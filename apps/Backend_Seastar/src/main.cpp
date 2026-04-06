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
#include <nlohmann/json.hpp>
#include <random>
#include <iomanip>
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

// Boost program options
#include <boost/program_options.hpp>
namespace bpo = boost::program_options;

using json = nlohmann::json;

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
// Genreration automatique du id si le type de la base de donnee est memory
// ─────────────────────────────────────────────────────────────
[[nodiscard]] std::int64_t generate_int_id(
    const std::string& entity_name,
    std::shared_ptr<GenericCrudEngine> crud_engine)
{
    const auto records = crud_engine->list(entity_name);
    std::int64_t max_id = 0;
    for (const auto& record : records) {
        auto it = record.find("id");
        if (it != record.end() && std::holds_alternative<std::int64_t>(it->second)) {
            max_id = std::max(max_id, std::get<std::int64_t>(it->second));
        }
    }
    return max_id + 1;
}
[[nodiscard]] std::string generate_uuid() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;

    uint64_t part1 = dis(gen);
    uint64_t part2 = dis(gen);

    part1 = (part1 & 0xffffffffffff0fffULL) | 0x0000000000004000ULL;
    part2 = (part2 & 0x3fffffffffffffffULL) | 0x8000000000000000ULL;

    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(8) << (part1 >> 32) << "-"
        << std::setw(4) << ((part1 >> 16) & 0xffff) << "-"
        << std::setw(4) << (part1 & 0xffff) << "-"
        << std::setw(4) << (part2 >> 48) << "-"
        << std::setw(12) << (part2 & 0xffffffffffffULL);
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
        rep->write_body("application/json", "{\"status\":\"RUNNING\"}");
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
class GetWithChildrenHandler final : public seastar::httpd::handler_base {
public:
    GetWithChildrenHandler(
        std::shared_ptr<GenericCrudEngine> crud_engine,
        std::string parent_entity,
        std::string child_entity,
        std::string fk_column,
        std::string children_key)
        : crud_engine_(std::move(crud_engine)),
        parent_entity_(std::move(parent_entity)),
        child_entity_(std::move(child_entity)),
        fk_column_(std::move(fk_column)),
        children_key_(std::move(children_key)) {}

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override {

        const auto parent_id = req->get_path_param("id");
        if (parent_id.empty()) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("application/json",
                            json{{"error", "Parametre 'id' manquant."}}.dump());
            co_return std::move(rep);
        }

        // Récupérer le parent
        const auto parent = crud_engine_->get_by_id(parent_entity_, std::string(parent_id));
        if (!parent.has_value()) {
            rep->set_status(seastar::http::reply::status_type::not_found);
            rep->write_body("application/json",
                            json{{"error", parent_entity_ + " introuvable."}}.dump());
            co_return std::move(rep);
        }

        // Construire le JSON du parent
        json result = json::parse(record_to_json(*parent));

        // Filtrer les enfants par FK
        const auto all_children = crud_engine_->list(child_entity_);
        json children = json::array();
        for (const auto& record : all_children) {
            const auto it = record.find(fk_column_);
            if (it == record.end()) continue;
            if (std::holds_alternative<std::string>(it->second) &&
                std::get<std::string>(it->second) == std::string(parent_id)) {
                children.push_back(json::parse(record_to_json(record)));
            }
        }

        // Ajouter les enfants dans le parent
        result[children_key_] = children;

        rep->set_status(seastar::http::reply::status_type::ok);
        rep->write_body("application/json", result.dump());
        co_return std::move(rep);
    }

private:
    std::shared_ptr<GenericCrudEngine> crud_engine_;
    std::string parent_entity_;
    std::string child_entity_;
    std::string fk_column_;
    std::string children_key_; // ex: "employees"
};
class ListByFkHandler final : public seastar::httpd::handler_base {
public:
    ListByFkHandler(
        std::shared_ptr<GenericCrudEngine> crud_engine,
        std::string child_entity,
        std::string fk_column)
        : crud_engine_(std::move(crud_engine)),
        child_entity_(std::move(child_entity)),
        fk_column_(std::move(fk_column)) {}

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override {

        const auto parent_id = req->get_path_param("id");
        if (parent_id.empty()) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("application/json",
                            json{{"error", "Parametre 'id' manquant."}}.dump());
            co_return std::move(rep);
        }

        const auto all_records = crud_engine_->list(child_entity_);
        std::vector<DynamicRecord> filtered;
        for (const auto& record : all_records) {
            const auto it = record.find(fk_column_);
            if (it == record.end()) continue;
            if (std::holds_alternative<std::string>(it->second) &&
                std::get<std::string>(it->second) == std::string(parent_id)) {
                filtered.push_back(record);
            }
        }

        rep->set_status(seastar::http::reply::status_type::ok);
        rep->write_body("application/json", records_to_json(filtered));
        co_return std::move(rep);
    }

private:
    std::shared_ptr<GenericCrudEngine> crud_engine_;
    std::string child_entity_;
    std::string fk_column_;
};
class ListByFkFieldHandler final : public seastar::httpd::handler_base {
public:
    ListByFkFieldHandler(
        std::shared_ptr<GenericCrudEngine> crud_engine,
        std::string child_entity,
        std::string parent_entity,
        std::string fk_column,
        std::string search_field)
        : crud_engine_(std::move(crud_engine)),
        child_entity_(std::move(child_entity)),
        parent_entity_(std::move(parent_entity)),
        fk_column_(std::move(fk_column)),
        search_field_(std::move(search_field)) {}

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override {

        const auto search_value = req->get_path_param(search_field_);
        std::cerr << "[LISTBYFK] search_field=" << search_field_
                  << " search_value=" << search_value
                  << " parent_entity=" << parent_entity_ << std::endl;
        if (search_value.empty()) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("application/json",
                            json{{"error", "Parametre '" + search_field_ + "' manquant."}}.dump());
            co_return std::move(rep);
        }

        // Trouver le parent par le champ unique
        const auto all_parents = crud_engine_->list(parent_entity_);
        std::string parent_id;
        for (const auto& record : all_parents) {
            const auto it = record.find(search_field_);
            if (it == record.end()) continue;
            if (std::holds_alternative<std::string>(it->second) &&
                std::get<std::string>(it->second) == std::string(search_value)) {
                const auto id_it = record.find("id");
                if (id_it != record.end() &&
                    std::holds_alternative<std::string>(id_it->second)) {
                    parent_id = std::get<std::string>(id_it->second);
                }
                break;
            }
        }

        if (parent_id.empty()) {
            rep->set_status(seastar::http::reply::status_type::not_found);
            rep->write_body("application/json",
                            json{{"error", parent_entity_ + " introuvable avec " +
                                               search_field_ + "=" + std::string(search_value)}}.dump());
            co_return std::move(rep);
        }

        // Filtrer les enfants par FK
        const auto all_children = crud_engine_->list(child_entity_);
        std::vector<DynamicRecord> filtered;
        for (const auto& record : all_children) {
            const auto it = record.find(fk_column_);
            if (it == record.end()) continue;
            if (std::holds_alternative<std::string>(it->second) &&
                std::get<std::string>(it->second) == parent_id) {
                filtered.push_back(record);
            }
        }

        rep->set_status(seastar::http::reply::status_type::ok);
        rep->write_body("application/json", records_to_json(filtered));
        co_return std::move(rep);
    }

private:
    std::shared_ptr<GenericCrudEngine> crud_engine_;
    std::string child_entity_;
    std::string parent_entity_;
    std::string fk_column_;
    std::string search_field_;
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
        std::cerr << "[DEBUG] url = " << req->get_url() << std::endl;
        std::cerr << "[DEBUG] query id = [" << req->get_query_param("id") << "]" << std::endl;
        std::cerr << "[DEBUG] path id  = [" << req->get_path_param("id") << "]" << std::endl;
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
    CreateHandler(
        std::shared_ptr<GenericCrudEngine> crud_engine,
        std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry> registry,
        std::string entity_name, sea::domain::DatabaseType db_type)
        : crud_engine_(std::move(crud_engine)),
        registry_(std::move(registry)),
        entity_name_(std::move(entity_name)),
        db_type_(std::move(db_type))  {
    }

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override {
        std::cerr << "[HANDLER] CreateHandler for entity = "
                  << entity_name_ << std::endl;

        const auto* entity = registry_->find_entity(entity_name_);
        if (!entity) {
            rep->set_status(seastar::http::reply::status_type::not_found);
            rep->write_body("text/plain", "Entite inconnue.");
            co_return std::move(rep);
        }

        try {
            // Version MVP : on garde req->content.
            // Oui, c'est deprecated, mais on corrige d'abord le POST.
            // On passera ensuite a content_stream.
            sea::infrastructure::runtime::JsonRecordParser parser;
            auto record = parser.parse(*entity, std::string(req->content));

            // ── Génération automatique de l'id ──────────────────────────
            if (db_type_ == sea::domain::DatabaseType::Memory) {
                const sea::domain::Field* id_field = nullptr;
                for (const auto& field : entity->fields) {
                    if (field.name == "id") { id_field = &field; break; }
                }

                if (id_field) {
                    if (id_field->type == sea::domain::FieldType::UUID) {
                        std::string new_id;
                        for (int i = 0; i < 5; ++i) {
                            new_id = generate_uuid();
                            if (!crud_engine_->get_by_id(entity_name_, new_id).has_value()) break;
                        }
                        record["id"] = new_id;
                    } else if (id_field->type == sea::domain::FieldType::Int) {
                        record["id"] = generate_int_id(entity_name_, crud_engine_);
                    }
                }
            }
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
            auto json_result = record_to_json(*result.record);
            std::cerr << "[UPDATE] json result = " << json_result << std::endl;
            rep->write_body("application/json", record_to_json(*result.record));
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
    sea::domain::DatabaseType db_type_;

};
class UpdateHandler final : public seastar::httpd::handler_base {
public:
    UpdateHandler(
        std::shared_ptr<GenericCrudEngine> crud_engine,
        std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry> registry,
        std::string entity_name)
        : crud_engine_(std::move(crud_engine)),
        registry_(std::move(registry)),
        entity_name_(std::move(entity_name)) {}

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

        const auto* entity = registry_->find_entity(entity_name_);
        if (!entity) {
            rep->set_status(seastar::http::reply::status_type::not_found);
            rep->write_body("text/plain", "Entite inconnue.");
            co_return std::move(rep);
        }

        try {
            sea::infrastructure::runtime::JsonRecordParser parser;
            auto record = parser.parse(*entity, std::string(req->content));
            std::cerr << "[UPDATE] body recu = " << std::string(req->content) << std::endl;
            const auto result = crud_engine_->update(entity_name_, std::string(id), std::move(record));

            if (!result.success) {
                std::ostringstream oss;
                oss << "{ \"errors\": [";
                for (std::size_t i = 0; i < result.errors.size(); ++i) {
                    if (i != 0) oss << ",";
                    oss << "\"" << json_escape(result.errors[i]) << "\"";
                }
                oss << "] }";
                rep->set_status(seastar::http::reply::status_type::bad_request);
                rep->write_body("application/json", oss.str());
                co_return std::move(rep);
            }

            rep->set_status(seastar::http::reply::status_type::ok);
            rep->write_body("application/json", record_to_json(*result.record));
            co_return std::move(rep);

        } catch (const std::exception& e) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("application/json", json{{"bad_request", std::string("Erreur JSON: ") + e.what()}}.dump());
            co_return std::move(rep);
        }
    }

private:
    std::shared_ptr<GenericCrudEngine> crud_engine_;
    std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry> registry_;
    std::string entity_name_;
};

class DeleteHandler final : public seastar::httpd::handler_base {
public:
    DeleteHandler(std::shared_ptr<GenericCrudEngine> crud_engine, std::string entity_name)
        : crud_engine_(std::move(crud_engine)),
        entity_name_(std::move(entity_name)) {}

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override {

        const auto id = req->get_path_param("id");
        std::cerr << "[HANDLER] DeleteHandler for entity = "
                  << entity_name_ << " id = " << id << std::endl;

        if (id.empty()) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("application/json", json{{"error", "Parametre 'id' manquant."}}.dump());
            co_return std::move(rep);
        }
        try {
            const bool deleted = crud_engine_->remove(entity_name_, std::string(id));
            if (!deleted) {
                rep->set_status(seastar::http::reply::status_type::not_found);
                rep->write_body("application/json", json{{"error", "Enregistrement introuvable."}}.dump());
                co_return std::move(rep);
            }
            rep->set_status(seastar::http::reply::status_type::ok);
            rep->write_body("application/json", json{{"message", "Deleted"}}.dump());
            co_return std::move(rep);

        } catch (const std::exception& e) {
            rep->set_status(seastar::http::reply::status_type::internal_server_error);
            rep->write_body("application/json", json{{"error", e.what()}}.dump());
            co_return std::move(rep);
        }
    }

private:
    std::shared_ptr<GenericCrudEngine> crud_engine_;
    std::string entity_name_;
};
} // namespace

int main(int argc, char** argv) {
    seastar::app_template app;

    app.add_options()
        ("config", bpo::value<std::string>()->required(), "Chemin du fichier YAML")
        ("service_name", bpo::value<std::string>()->required(), "Nom du service a lancer");

    return app.run(argc, argv, [&app] {
        const auto& cfg = app.configuration();

        const std::string config_path = cfg["config"].as<std::string>();
        const std::string service_name = cfg["service_name"].as<std::string>();

        std::cerr << "[1] config = " << config_path << std::endl;
        std::cerr << "[2] service = " << service_name << std::endl;

        sea::application::ImportYamlSchemaUseCase importer;
        const auto project = importer.execute(config_path);

        if (project.empty()) {
            throw std::runtime_error("Aucun service defini dans le projet.");
        }

        const sea::domain::Service* selected_service = nullptr;

        for (const auto& service : project.services) {
            if (service.name == service_name) {
                selected_service = &service;
                break;
            }
        }

        if (!selected_service) {
            throw std::runtime_error("Service introuvable: " + service_name);
        }

        const auto& service = *selected_service;

        sea::application::ValidateSchemaUseCase validate_usecase;
        const auto validation = validate_usecase.execute(service);

        if (!validation.valid) {
            std::ostringstream oss;
            oss << "Schema invalide : ";
            for (const auto& err : validation.errors) {
                oss << err << " ; ";
            }
            throw std::runtime_error(oss.str());
        }

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

        sea::application::RouteGenerator route_generator;
        const auto route_definitions = route_generator.generate(service.schema);
        std::cerr << "========== ROUTES POUR " << service.name << " ==========" << std::endl;

        for (const auto& route : route_definitions) {
            const char* method = "UNKNOWN";

            switch (route.method) {
            case sea::application::HttpMethod::Get:    method = "GET"; break;
            case sea::application::HttpMethod::Post:   method = "POST"; break;
            case sea::application::HttpMethod::Put:    method = "PUT"; break;
            case sea::application::HttpMethod::Delete: method = "DELETE"; break;
            }

            std::cerr << method
                      << "  " << route.path
                      << "  [" << route.entity_name
                      << " / " << route.operation_name << "]"
                      << std::endl;
        }

        std::cerr << "==========================================" << std::endl;
        auto server = std::make_shared<seastar::httpd::http_server_control>();

        return server->start()
            .then([server, crud_engine, registry, route_definitions, service] {
                std::cerr << "[9] apres start(), avant set_routes()" << std::endl;

                return server->set_routes(
                    [crud_engine, registry, route_definitions, service](seastar::httpd::routes& r) {
                        std::cerr << "[10] dans set_routes()" << std::endl;

                        // Route de santé
                        r.add(
                            seastar::httpd::operation_type::GET,
                            seastar::httpd::url("/health"),
                            new HealthHandler()
                            );

                        // =========================================================
                        // PASS 1 : routes sans id
                        // =========================================================
                        for (const auto& route : route_definitions) {
                            if (route.method == sea::application::HttpMethod::Get &&
                                route.operation_name == "list") {
                                std::cerr << "[ROUTE] GET " << route.path
                                          << " -> ListHandler" << std::endl;

                                r.add(
                                    seastar::httpd::operation_type::GET,
                                    seastar::httpd::url(route.path),
                                    new ListHandler(crud_engine, route.entity_name)
                                    );
                            }

                            if (route.method == sea::application::HttpMethod::Post &&
                                route.operation_name == "create") {
                                std::cerr << "[ROUTE] POST " << route.path
                                          << " -> CreateHandler" << std::endl;

                                r.add(
                                    seastar::httpd::operation_type::POST,
                                    seastar::httpd::url(route.path),
                                    new CreateHandler(crud_engine, registry, route.entity_name, service.database_config.type)
                                    );
                            }
                        }
                        // =========================================================
                        // PASS 2 : routes HasMany
                        // =========================================================
                        std::cerr << "[PASS2] nb entities=" << service.schema.entities.size() << std::endl;
                        for (const auto& entity_def : service.schema.entities) {
                            std::cerr << "[PASS2] entity=" << entity_def.name
                                      << " nb relations=" << entity_def.relations.size() << std::endl;
                            for (const auto& relation : entity_def.relations) {
                                std::cerr << "[PASS2] relation=" << relation.name
                                          << " kind=" << static_cast<int>(relation.kind) << std::endl;
                            }
                        }
                        for (const auto& entity_def : service.schema.entities) {  // ← boucle manquante
                            for (const auto& relation : entity_def.relations) {
                                if (relation.kind != sea::domain::RelationKind::HasMany) continue;

                                std::string child_path = "/" + relation.target_entity;
                                child_path[1] = static_cast<char>(std::tolower(child_path[1]));
                                child_path += "s";

                                std::string parent_name = entity_def.name;
                                parent_name[0] = static_cast<char>(std::tolower(parent_name[0]));

                                // Route par id
                                std::string route_path = child_path + "/filter/with_" + parent_name;
                                r.add(
                                    seastar::httpd::operation_type::GET,
                                    seastar::httpd::url(route_path).remainder("id"),
                                    new ListByFkHandler(crud_engine, relation.target_entity, relation.fk_column)
                                    );

                                // Trouver l'entité parent pour accéder à ses champs
                                const sea::domain::Entity* parent_entity = nullptr;
                                for (const auto& e : service.schema.entities) {
                                    if (e.name == entity_def.name) {
                                        parent_entity = &e;
                                        break;
                                    }
                                }

                                if (!parent_entity) continue;

                                // Route par chaque champ unique (sauf id)
                                for (const auto& field : parent_entity->fields) {
                                    if (!field.unique || field.name == "id") continue;

                                    std::string route_by_field = child_path + "/filter/with_" + parent_name + "_" + field.name;

                                    std::cerr << "[ROUTE] GET " << route_by_field << "/<" << field.name << ">"
                                              << " -> ListByFkFieldHandler" << std::endl;

                                    r.add(
                                        seastar::httpd::operation_type::GET,
                                        seastar::httpd::url(route_by_field).remainder(field.name),
                                        new ListByFkFieldHandler(
                                            crud_engine,
                                            relation.target_entity,
                                            entity_def.name,
                                            relation.fk_column,
                                            field.name
                                            )
                                        );
                                }
                                // Route department_with_employees
                                std::string parent_path = "/" + entity_def.name;
                                parent_path[1] = static_cast<char>(std::tolower(parent_path[1]));
                                parent_path += "s";

                                std::string relation_name = relation.name; // ex: "employees"
                                std::string route_with_children = parent_path + "_with_" + relation_name;

                                std::cerr << "[ROUTE] GET " << route_with_children << "/<id>"
                                          << " -> GetWithChildrenHandler" << std::endl;

                                r.add(
                                    seastar::httpd::operation_type::GET,
                                    seastar::httpd::url(route_with_children).remainder("id"),
                                    new GetWithChildrenHandler(
                                        crud_engine,
                                        entity_def.name,        // parent = Department
                                        relation.target_entity, // child = Employee
                                        relation.fk_column,     // fk = department_id
                                        relation.name           // key = employees
                                        )
                                    );
                            }
                        }  // ← fermeture boucle entity_def
                        // =========================================================
                        // PASS 3 : routes avec id
                        // =========================================================
                        for (const auto& route : route_definitions) {
                            if (route.method == sea::application::HttpMethod::Get &&
                                route.operation_name == "get_by_id") {

                                std::string base_path = "/" + route.entity_name;
                                base_path[1] = static_cast<char>(std::tolower(base_path[1]));
                                base_path += "s";

                                std::cerr << "[ROUTE] GET " << base_path << "/<id>"
                                          << " -> GetByIdHandler" << std::endl;

                                r.add(
                                    seastar::httpd::operation_type::GET,
                                    seastar::httpd::url(base_path).remainder("id"),
                                    new GetByIdHandler(crud_engine, route.entity_name)
                                    );
                            }

                            if (route.method == sea::application::HttpMethod::Put &&
                                route.operation_name == "update") {
                                std::cerr << "[ROUTE] PUT " << route.path
                                          << " -> UpdateHandler" << std::endl;
                                std::string base_path = "/" + route.entity_name;
                                base_path[1] = static_cast<char>(std::tolower(base_path[1]));
                                base_path += "s";

                                r.add(
                                    seastar::httpd::operation_type::PUT,
                                    seastar::httpd::url(base_path).remainder("id"),
                                    new UpdateHandler(crud_engine, registry, route.entity_name)
                                    );
                            }

                            if (route.method == sea::application::HttpMethod::Delete &&
                                route.operation_name == "delete") {
                                std::string base_path = "/" + route.entity_name;
                                base_path[1] = static_cast<char>(std::tolower(base_path[1]));
                                base_path += "s";
                                std::cerr << "[ROUTE] DELETE " << base_path << "/<id>"
                                          << " -> DeleteHandler" << std::endl;

                                r.add(
                                    seastar::httpd::operation_type::DELETE,
                                    seastar::httpd::url(base_path).remainder("id"),
                                    new DeleteHandler(crud_engine, route.entity_name)
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