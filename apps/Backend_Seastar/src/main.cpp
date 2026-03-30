#include <seastar/core/app-template.hh>
#include <seastar/core/sleep.hh>
#include <seastar/http/handlers.hh>
#include <seastar/http/httpd.hh>

#include <chrono>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <variant>

// Application
#include "import_yaml_schema_usecase.h"
#include "route_generator.h"
#include "start_service_usecase.h"
#include "validate_schema_usecase.h"

// Infrastructure
#include "persistence/repository_factory.h"
#include "runtime/generic_crud_engine.h"
#include "runtime/generic_validator.h"
#include "runtime/json_record_parser.h"
#include "runtime/schema_runtime_registry.h"

namespace {

using sea::infrastructure::runtime::DynamicRecord;
using sea::infrastructure::runtime::DynamicValue;
using sea::infrastructure::runtime::GenericCrudEngine;

constexpr bool kEnableCrudRoutes = true;

// ─────────────────────────────────────────────────────────────
// Helpers JSON
// ─────────────────────────────────────────────────────────────

[[nodiscard]] std::string json_escape(const std::string& input) {
    std::string out;
    out.reserve(input.size());

    for (char c : input) {
        switch (c) {
        case '\"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:   out += c; break;
        }
    }

    return out;
}

[[nodiscard]] std::string dynamic_value_to_json(const DynamicValue& value) {
    if (std::holds_alternative<std::monostate>(value)) {
        return "null";
    }

    if (std::holds_alternative<std::string>(value)) {
        return "\"" + json_escape(std::get<std::string>(value)) + "\"";
    }

    if (std::holds_alternative<std::int64_t>(value)) {
        return std::to_string(std::get<std::int64_t>(value));
    }

    if (std::holds_alternative<double>(value)) {
        std::ostringstream oss;
        oss << std::get<double>(value);
        return oss.str();
    }

    if (std::holds_alternative<bool>(value)) {
        return std::get<bool>(value) ? "true" : "false";
    }

    return "null";
}

[[nodiscard]] std::string record_to_json(const DynamicRecord& record) {
    std::ostringstream oss;
    oss << "{";

    bool first = true;
    for (const auto& [key, value] : record) {
        if (!first) {
            oss << ",";
        }
        first = false;
        oss << "\"" << json_escape(key) << "\":" << dynamic_value_to_json(value);
    }

    oss << "}";
    return oss.str();
}

[[nodiscard]] std::string records_to_json(const std::vector<DynamicRecord>& records) {
    std::ostringstream oss;
    oss << "[";

    bool first = true;
    for (const auto& record : records) {
        if (!first) {
            oss << ",";
        }
        first = false;
        oss << record_to_json(record);
    }

    oss << "]";
    return oss.str();
}

// ─────────────────────────────────────────────────────────────
// Handlers HTTP
// ─────────────────────────────────────────────────────────────

class HealthHandler final : public seastar::httpd::handler_base {
public:
    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request>,
           std::unique_ptr<seastar::http::reply> rep) override {
        rep->set_status(seastar::http::reply::status_type::ok);
        rep->write_body("text/plain", "OK");
        co_return std::move(rep);
    }
};

class ListHandler final : public seastar::httpd::handler_base {
public:
    ListHandler(std::shared_ptr<GenericCrudEngine> crud_engine, std::string entity_name)
        : crud_engine_(std::move(crud_engine)),
        entity_name_(std::move(entity_name)) {
    }

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request>,
           std::unique_ptr<seastar::http::reply> rep) override {
        const auto records = crud_engine_->list(entity_name_);
        rep->set_status(seastar::http::reply::status_type::ok);
        rep->write_body("application/json", records_to_json(records));
        co_return std::move(rep);
    }

private:
    std::shared_ptr<GenericCrudEngine> crud_engine_;
    std::string entity_name_;
};

class GetByIdHandler final : public seastar::httpd::handler_base {
public:
    GetByIdHandler(std::shared_ptr<GenericCrudEngine> crud_engine, std::string entity_name)
        : crud_engine_(std::move(crud_engine)),
        entity_name_(std::move(entity_name)) {
    }

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override {
        const auto id = req->get_path_param("id");

        if (id.empty()) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("text/plain", "Parametre 'id' manquant.");
            co_return std::move(rep);
        }

        const auto record = crud_engine_->get_by_id(entity_name_, std::string(id));

        if (!record.has_value()) {
            rep->set_status(seastar::http::reply::status_type::not_found);
            rep->write_body("text/plain", "Enregistrement introuvable.");
            co_return std::move(rep);
        }

        rep->set_status(seastar::http::reply::status_type::ok);
        rep->write_body("application/json", record_to_json(*record));
        co_return std::move(rep);
    }

private:
    std::shared_ptr<GenericCrudEngine> crud_engine_;
    std::string entity_name_;
};

class CreateHandler final : public seastar::httpd::handler_base {
public:
    CreateHandler(std::shared_ptr<GenericCrudEngine> crud_engine,
                  std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry> registry,
                  std::string entity_name)
        : crud_engine_(std::move(crud_engine)),
        registry_(std::move(registry)),
        entity_name_(std::move(entity_name)) {
    }

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override {
        const auto* entity = registry_->find_entity(entity_name_);
        if (!entity) {
            rep->set_status(seastar::http::reply::status_type::not_found);
            rep->write_body("text/plain", "Entite inconnue.");
            co_return std::move(rep);
        }

        try {
            sea::infrastructure::runtime::JsonRecordParser parser;
            auto record = parser.parse(*entity, req->content);

            const auto result = crud_engine_->create(entity_name_, std::move(record));

            if (!result.success) {
                std::ostringstream oss;
                oss << "{ \"errors\": [";
                for (std::size_t i = 0; i < result.errors.size(); ++i) {
                    if (i != 0) {
                        oss << ",";
                    }
                    oss << "\"" << json_escape(result.errors[i]) << "\"";
                }
                oss << "] }";

                rep->set_status(seastar::http::reply::status_type::bad_request);
                rep->write_body("application/json", oss.str());
                co_return std::move(rep);
            }

            rep->set_status(seastar::http::reply::status_type::created);
            rep->write_body("text/plain", "Created");
            co_return std::move(rep);
        } catch (const std::exception& e) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("text/plain", std::string("Erreur JSON: ") + e.what());
            co_return std::move(rep);
        }
    }

private:
    std::shared_ptr<GenericCrudEngine> crud_engine_;
    std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry> registry_;
    std::string entity_name_;
};

} // namespace

int main(int argc, char** argv) {
    seastar::app_template app;

    return app.run(argc, argv, [] {
        std::cerr << "[1] debut" << std::endl;

        // 1. Import YAML
        sea::application::ImportYamlSchemaUseCase importer;
        std::cerr << "[2] avant import yaml" << std::endl;

        const auto project = importer.execute(
            "/home/frederic/QtProjects/SeaDesktop/configs/SeaDesktopDemo.yaml"
            );

        std::cerr << "[3] apres import yaml" << std::endl;

        if (project.empty()) {
            throw std::runtime_error("Aucun service defini dans le projet.");
        }

        const auto service = project.services.front();
        std::cerr << "[4] service: " << service.name << std::endl;

        // 2. Validation
        sea::application::ValidateSchemaUseCase validate_usecase;
        const auto validation = validate_usecase.execute(service);

        std::cerr << "[5] apres validation" << std::endl;

        if (!validation.valid) {
            std::ostringstream oss;
            oss << "Schema invalide : ";
            for (const auto& err : validation.errors) {
                oss << err << " ; ";
            }
            throw std::runtime_error(oss.str());
        }

        // 3. Runtime + persistence
        auto registry =
            std::make_shared<sea::infrastructure::runtime::SchemaRuntimeRegistry>();

        auto repository_factory =
            std::make_shared<sea::infrastructure::persistence::RepositoryFactory>();

        sea::application::StartServiceUseCase start_usecase(*registry, *repository_factory);
        auto repository =
            std::shared_ptr<sea::infrastructure::persistence::IGenericRepository>(
                std::move(start_usecase.execute(service))
                );

        auto validator =
            std::make_shared<sea::infrastructure::runtime::GenericValidator>();

        auto crud_engine =
            std::make_shared<sea::infrastructure::runtime::GenericCrudEngine>(
                registry,
                validator,
                repository
                );

        // 4. Routes logiques
        sea::application::RouteGenerator route_generator;
        const auto route_definitions = route_generator.generate(service.schema);

        std::cerr << "[6] routes generees: " << route_definitions.size() << std::endl;
        for (const auto& route : route_definitions) {
            std::cerr << "    -> " << route.path
                      << " [" << route.entity_name
                      << " / " << route.operation_name << "]" << std::endl;
        }

        // 5. Serveur HTTP
        std::cerr << "[7] avant creation server" << std::endl;
        auto server = std::make_shared<seastar::httpd::http_server_control>();
        std::cerr << "[8] apres creation server" << std::endl;

        return server->start()
            .then([server, crud_engine, registry, route_definitions] {
                std::cerr << "[9] apres start(), avant set_routes()" << std::endl;

                return server->set_routes(
                    [crud_engine, registry, route_definitions](seastar::httpd::routes& r) {
                        std::cerr << "[10] dans set_routes()" << std::endl;

                        r.add(
                            seastar::httpd::operation_type::GET,
                            seastar::httpd::url("/health"),
                            new HealthHandler()
                            );

                        if (!kEnableCrudRoutes) {
                            std::cerr << "[11] mode health-only" << std::endl;
                            return;
                        }

                        for (const auto& route : route_definitions) {
                            if (route.method == sea::application::HttpMethod::Get &&
                                route.operation_name == "list") {
                                r.add(
                                    seastar::httpd::operation_type::GET,
                                    seastar::httpd::url(route.path),
                                    new ListHandler(crud_engine, route.entity_name)
                                    );
                            }

                            if (route.method == sea::application::HttpMethod::Get &&
                                route.operation_name == "get_by_id") {
                                r.add(
                                    seastar::httpd::operation_type::GET,
                                    seastar::httpd::url(route.path),
                                    new GetByIdHandler(crud_engine, route.entity_name)
                                    );
                            }

                            if (route.method == sea::application::HttpMethod::Post &&
                                route.operation_name == "create") {
                                r.add(
                                    seastar::httpd::operation_type::POST,
                                    seastar::httpd::url(route.path),
                                    new CreateHandler(crud_engine, registry, route.entity_name)
                                    );
                            }
                        }

                        std::cerr << "[12] routes CRUD ajoutees" << std::endl;
                    }
                    );
            })
            .then([server, service] {
                std::cerr << "[13] avant listen() sur port " << service.port << std::endl;
                return server->listen(seastar::ipv4_addr{service.port});
            })
            .then([] {
                std::cerr << "[14] serveur demarre" << std::endl;
                return seastar::sleep(std::chrono::hours(24 * 365));
            })
            .handle_exception([server](std::exception_ptr ep) {
                try {
                    std::rethrow_exception(ep);
                } catch (const std::exception& e) {
                    std::cerr << "Exception capturee: " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "Exception inconnue capturee." << std::endl;
                }

                return server->stop().handle_exception([](std::exception_ptr) {
                    return seastar::make_ready_future<>();
                });
            });
    });
}