#include <seastar/core/app-template.hh>
#include <seastar/core/sleep.hh>
#include <seastar/http/httpd.hh>

#include <boost/program_options.hpp>

#include <chrono>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

#include "authservice.h"
#include "http/handlers/auth_handlers/login_handler.h"
#include "http/handlers/auth_handlers/me_handler.h"
#include "http/handlers/auth_handlers/protected_handler.h"
#include "http/handlers/auth_handlers/register_handler.h"
#include "http/handlers/misc_handlers/health_handler.h"
#include "http/handlers/misc_handlers/openapi_handler.h"
#include "http/handlers/misc_handlers/swagger_ui_handler.h"
#include "import_yaml_schema_usecase.h"
#include "openapigenerator.h"
#include "route_generator.h"
#include "validate_schema_usecase.h"

#include "persistence/repository_factory.h"
#include "persistence/mysql/mysql_connector.h"
#include "persistence/mysql/mysqlconnexionpool.h"

#include "runtime/generic_crud_engine.h"
#include "runtime/generic_validator.h"
#include "runtime/schema_runtime_registry.h"
#include "security/secret_store.h"

#include "http/routing/route_registration.h"

#include "http/handlers/misc_handlers/health_handler.h"
#include "http/handlers/misc_handlers/openapi_handler.h"
#include "http/handlers/misc_handlers/swagger_ui_handler.h"

#include "http/handlers/auth_handlers/register_handler.h"
#include "http/handlers/auth_handlers/login_handler.h"
#include "http/handlers/auth_handlers/me_handler.h"
#include "http/handlers/auth_handlers/protected_handler.h"

namespace bpo = boost::program_options;

int main(int argc, char** argv)
{
    seastar::app_template app;

    app.add_options()
        ("config",
         bpo::value<std::string>()->default_value("config/project.yaml"),
         "Chemin du fichier YAML")
        ("service_name",
         bpo::value<std::string>()->default_value("CCNBService"),
         "Nom du service a demarrer");

    return app.run(argc, argv, [&app]() -> seastar::future<> {
        const auto& cfg = app.configuration();

        const std::string config_path = cfg["config"].as<std::string>();
        const std::string service_name = cfg["service_name"].as<std::string>();

        // std::cerr << "[BOOT] config = " << config_path << "\n";
        // std::cerr << "[BOOT] service = " << service_name << "\n";

        sea::application::ImportYamlSchemaUseCase importer;
        const auto project = importer.execute(config_path);

        if (project.services.empty()) {
            throw std::runtime_error("Aucun service defini dans le projet.");
        }

        const sea::domain::Service* selected_service = nullptr;

        for (const auto& service : project.services) {
            if (service.name == service_name) {
                selected_service = &service;
                break;
            }
        }

        if (selected_service == nullptr) {
            throw std::runtime_error("Service introuvable: " + service_name);
        }

        const auto service = *selected_service;

        sea::application::ValidateSchemaUseCase validate_usecase;
        const auto validation = validate_usecase.execute(service);

        if (!validation.valid) {
            std::ostringstream oss;
            oss << "Schema invalide: ";

            for (const auto& error : validation.errors) {
                oss << error << " ; ";
            }

            throw std::runtime_error(oss.str());
        }

        auto registry =
            std::make_shared<sea::infrastructure::runtime::SchemaRuntimeRegistry>();

        registry->register_schema(service.schema);

        auto mysql_pool =
            std::make_shared<seastar::sharded<
                sea::infrastructure::persistence::mysql::MysqlConnexionPool>>();

        sea::infrastructure::persistence::RepositoryFactory::DatabaseResources resources;

        if (service.database_config.type == sea::domain::DatabaseType::MySQL) {
            sea::infrastructure::persistence::mysql::MySQLConnector connector(
                service.database_config.host,
                service.database_config.username,
                service.database_config.password,
                service.database_config.database_name,
                static_cast<unsigned int>(service.database_config.port)
                );

            constexpr std::size_t pool_size = 2;

            co_await mysql_pool->start(std::move(connector), pool_size);
            co_await mysql_pool->invoke_on_all([](sea::infrastructure::persistence::mysql::MysqlConnexionPool& pool) {
                return pool.start();
            });
            resources.mysql_pool = mysql_pool.get();

            // std::cerr << "[BOOT] MySQL pool demarre\n";
        }

        sea::infrastructure::persistence::RepositoryFactory repository_factory;

        auto repository = repository_factory.create(
            service.database_config,
            registry,
            resources
            );

        auto runtime_validator =
            std::make_shared<sea::infrastructure::runtime::GenericValidator>();

        auto crud_engine =
            std::make_shared<sea::infrastructure::runtime::GenericCrudEngine>(
                registry,
                runtime_validator,
                repository
                );

        sea::application::RouteGenerator route_generator;
        const auto route_definitions = route_generator.generate(service.schema);

        sea::application::OpenApiGenerator openapi_generator;
        const auto openapi_doc = openapi_generator.generate(service, route_definitions);
        const auto openapi_json = openapi_doc.dump(2);

        sea::http::routing::log_route_definitions(
            service.name,
            route_definitions
            );

        sea::infrastructure::security::JwtSecretConfig secret_cfg;
        secret_cfg.storageDir = "./runtime/secrets";
        secret_cfg.serviceName = service.name;

        const auto jwt_secret =
            sea::infrastructure::security::resolve_jwt_secret(secret_cfg);

        auto auth_service =
            std::make_shared<sea::application::AuthService>(jwt_secret);

        auto server =
            std::make_shared<seastar::httpd::http_server_control>();

        co_await server->start()
            .then([server] {
                return server->server().invoke_on_all([](seastar::httpd::http_server& s) {
                    s.set_content_streaming(true);
                });
            })
            .then([server, crud_engine, registry, route_definitions, service, openapi_json, auth_service] {
                return server->set_routes(
                    [crud_engine, registry, route_definitions, service, openapi_json, auth_service]
                    (seastar::httpd::routes& r) {

                        r.add(
                            seastar::httpd::operation_type::GET,
                            seastar::httpd::url("/health"),
                            new sea::http::handlers::misc::HealthHandler()
                            );

                        r.add(
                            seastar::httpd::operation_type::GET,
                            seastar::httpd::url("/openapi.json"),
                            new sea::http::handlers::misc::OpenApiHandler(openapi_json)
                            );

                        r.add(
                            seastar::httpd::operation_type::GET,
                            seastar::httpd::url("/docs"),
                            new sea::http::handlers::misc::SwaggerUiHandler()
                            );

                        // ─────────────────────────────
                        // AUTH
                        // ─────────────────────────────

                        r.add(
                            seastar::httpd::operation_type::POST,
                            seastar::httpd::url("/auth/register"),
                            new sea::http::handlers::auth::RegisterHandler(
                                crud_engine,
                                registry,
                                auth_service,
                                service.database_config.type
                                )
                            );

                        r.add(
                            seastar::httpd::operation_type::POST,
                            seastar::httpd::url("/auth/login"),
                            new sea::http::handlers::auth::LoginHandler(
                                crud_engine,
                                auth_service
                                )
                            );

                        r.add(
                            seastar::httpd::operation_type::GET,
                            seastar::httpd::url("/auth/me"),
                            new sea::http::handlers::auth::ProtectedHandler(
                                std::make_unique<sea::http::handlers::auth::MeHandler>(
                                    crud_engine,
                                    auth_service
                                    ),
                                auth_service
                                )
                            );

                        // ─────────────────────────────
                        // CRUD COLLECTION (GET / POST)
                        // ─────────────────────────────

                        for (const auto& route : route_definitions) {
                            if (route.operation_name == "list" ||
                                route.operation_name == "create") {

                                sea::http::routing::register_collection_route(
                                    r,
                                    route,
                                    crud_engine,
                                    registry,
                                    service,
                                    auth_service
                                    );
                            }
                        }

                        // ─────────────────────────────
                        // CRUD ITEM (GET BY ID / UPDATE / DELETE)
                        // ─────────────────────────────

                        for (const auto& route : route_definitions) {
                            if (route.operation_name == "get_by_id" ||
                                route.operation_name == "update" ||
                                route.operation_name == "delete") {

                                sea::http::routing::register_item_route(
                                    r,
                                    route,
                                    crud_engine,
                                    registry,
                                    service,
                                    auth_service
                                    );
                            }
                        }

                        // ─────────────────────────────
                        // RELATIONS
                        // ─────────────────────────────

                        sea::http::routing::register_has_many_routes(
                            r,
                            crud_engine,
                            service,
                            auth_service
                            );

                        sea::http::routing::register_has_one_routes(
                            r,
                            crud_engine,
                            service,
                            auth_service
                            );

                        sea::http::routing::register_many_to_many_routes(
                            r,
                            crud_engine,
                            service,
                            auth_service
                            );

                    }
                    );
            })
            .then([server, service] {
                // std::cerr << "[BOOT] listen sur port " << service.port << "\n";
                return server->listen(seastar::ipv4_addr{service.port});
            })
            .then([] {
                // std::cerr << "[BOOT] serveur demarre\n";
                return seastar::sleep(std::chrono::hours(24 * 365));
            })
            .handle_exception([server, mysql_pool, service](std::exception_ptr ep) {
                try {
                    std::rethrow_exception(ep);
                } catch (const std::exception& e) {
                    // std::cerr << "[ERROR] " << e.what() << "\n";
                } catch (...) {
                    // std::cerr << "[ERROR] unknown exception\n";
                }

                return server->stop()
                    .then([mysql_pool, service] {
                        if (service.database_config.type == sea::domain::DatabaseType::MySQL) {
                            return mysql_pool->stop();
                        }
                        return seastar::make_ready_future<>();
                    });
            });

        co_await server->stop();

        if (service.database_config.type == sea::domain::DatabaseType::MySQL) {
            co_await mysql_pool->stop();
        }

        co_return;
    });
}