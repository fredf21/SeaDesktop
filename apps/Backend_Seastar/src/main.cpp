#include <seastar/core/app-template.hh>
#include <seastar/core/sleep.hh>
#include <seastar/core/sharded.hh>
#include <seastar/core/when_all.hh>
#include <seastar/core/reactor.hh>
#include <seastar/http/httpd.hh>

#include <boost/program_options.hpp>

#include <chrono>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

#include "authservice.h"
#include "import_yaml_schema_usecase.h"
#include "openapigenerator.h"
#include "route_generator.h"
#include "validate_schema_usecase.h"

// Handlers
#include "http/handlers/auth_handlers/login_handler.h"
#include "http/handlers/auth_handlers/me_handler.h"
#include "http/handlers/auth_handlers/register_handler.h"
#include "http/handlers/misc_handlers/health_handler.h"
#include "http/handlers/misc_handlers/openapi_handler.h"
#include "http/handlers/misc_handlers/swagger_ui_handler.h"
#include "http/handlers/misc_handlers/swagger_assets_handler.h"

// Middlewares
#include "http/middlewares/rate_limit_store.h"

// Persistence
#include "persistence/repository_factory.h"
#include "persistence/mysql/mysql_connector.h"
#include "persistence/mysql/mysqlconnexionpool.h"

// Blocking executor
#include "thread_pool_execution/std_thread_pool_executor.h"

// Runtime
#include "runtime/generic_crud_engine.h"
#include "runtime/generic_validator.h"
#include "runtime/schema_runtime_registry.h"

// Security
#include "security/secret_store.h"

// Routing
#include "http/routing/route_registration.h"
#include "access_control/policy_engine.h"
#include "access_control/operators/operator_registry.h"

namespace bpo = boost::program_options;

namespace {

bool is_main_shard()
{
    return seastar::this_shard_id() == 0;
}

void log_boot(const std::string& message)
{
    if (is_main_shard()) {
        std::cerr << message << "\n";
    }
}

} // namespace

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

        const std::string config_path =
            cfg["config"].as<std::string>();

        const std::string service_name =
            cfg["service_name"].as<std::string>();

        // ─────────────────────────────────────────────────────
        // 1. Charger le projet YAML
        // ─────────────────────────────────────────────────────
        sea::application::ImportYamlSchemaUseCase importer;
        const auto project = importer.execute(config_path);

        if (project.services.empty()) {
            throw std::runtime_error("Aucun service defini dans le projet.");
        }

        // ─────────────────────────────────────────────────────
        // 2. Sélectionner le service à démarrer
        // ─────────────────────────────────────────────────────
        const sea::domain::Service* selected_service = nullptr;

        for (const auto& s : project.services) {
            if (s.name == service_name) {
                selected_service = &s;
                break;
            }
        }

        if (selected_service == nullptr) {
            throw std::runtime_error("Service introuvable: " + service_name);
        }

        const auto service = *selected_service;
        // Après l'import du service depuis YAML
        const auto& ac_config = service.access_control;

        std::cerr << "[BOOT] Authorization: "
                  << (ac_config.enabled() ? "ENABLED" : "DISABLED") << "\n";

        if (ac_config.enabled()) {
            using namespace sea::domain::access_control;

            std::cerr << "  default_policy: " << to_string(ac_config.default_policy()) << "\n";
            std::cerr << "  admin_role: " << ac_config.admin_role() << "\n";
            std::cerr << "  default_scope_field: " << ac_config.default_scope_field() << "\n";

            std::cerr << "  declared_roles: ";
            for (const auto& r : ac_config.declared_roles()) {
                std::cerr << r << " ";
            }
            std::cerr << "\n";

            // Per-entity rules
            std::cerr << "[BOOT] Per-entity access control rules:\n";
            for (const auto& entity : service.schema.entities) {
                const auto& entity_ac = entity.access_control;
                if (!entity_ac.has_any_spec()) continue;

                std::cerr << "  " << entity.name;
                if (!entity_ac.scope_field().empty()) {
                    std::cerr << " (scope_field=" << entity_ac.scope_field() << ")";
                }
                if (!entity_ac.owner_field().empty()) {
                    std::cerr << " (owner_field=" << entity_ac.owner_field() << ")";
                }
                std::cerr << "\n";

                for (int op_idx = 0; op_idx <= 4; ++op_idx) {
                    const auto op = static_cast<CrudOperation>(op_idx);
                    const auto* spec = entity_ac.find_spec(op);
                    if (spec && !spec->is_empty()) {
                        std::cerr << "    " << to_string(op);
                        std::cerr << (spec->requires_resource() ? " (resource-aware)" : " (subject-only, fast path)");
                        std::cerr << "\n";
                    }
                }
            }
        }
        // ─────────────────────────────────────────────────────
        // 3. Valider le schéma
        // ─────────────────────────────────────────────────────
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

        // ─────────────────────────────────────────────────────
        // 4. Valider la configuration de sécurité
        // ─────────────────────────────────────────────────────
        try {
            service.security.validate();
        } catch (const std::exception& e) {
            throw std::runtime_error(
                std::string("Configuration de securite invalide pour le service '") +
                service.name + "': " + e.what()
                );
        }

        // ─────────────────────────────────────────────────────
        // 5. Registry runtime
        // ─────────────────────────────────────────────────────
        auto registry =
            std::make_shared<sea::infrastructure::runtime::SchemaRuntimeRegistry>();

        registry->register_schema(service.schema);

        // ─────────────────────────────────────────────────────
        // 6. Executor bloquant
        // ─────────────────────────────────────────────────────
        /**
         * Ce thread pool reçoit les opérations qui ne doivent jamais tourner
         * directement dans le reactor Seastar :
         *
         * - MySQL Connector/C++
         * - bcrypt / argon2
         * - vérification ou signature JWT si libcrypto est impliquée
         */
        auto blocking_executor =
            std::make_shared<StdThreadPoolExecutor>(4);

        // ─────────────────────────────────────────────────────
        // 7. Pool MySQL
        // ─────────────────────────────────────────────────────
        auto mysql_pool =
            std::make_shared<
                seastar::sharded<
                    sea::infrastructure::persistence::mysql::MysqlConnexionPool
                    >
                >();

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

            co_await mysql_pool->start(std::move(connector), pool_size, blocking_executor);

            /**
             * Attention :
             * si MysqlConnexionPool::start() crée des connexions synchrones,
             * il peut encore provoquer des stalls au démarrage.
             *
             * Ce sera le prochain fichier à corriger si les stalls apparaissent
             * avant le message "[BOOT] MySQL pool demarre".
             */
            co_await mysql_pool->invoke_on_all([](auto& pool) {
                return pool.start();
            });

            resources.mysql_pool = mysql_pool.get();

            log_boot("[BOOT] MySQL pool demarre");
        }

        // ─────────────────────────────────────────────────────
        // 8. RateLimitStore
        // ─────────────────────────────────────────────────────
        auto rate_limit_store =
            std::make_shared<
                seastar::sharded<sea::http::middlewares::RateLimitStore>
                >();

        const bool rate_limits_enabled =
            !service.security.rate_limits().empty();

        if (rate_limits_enabled) {
            co_await rate_limit_store->start();

            if (is_main_shard()) {
                std::cerr << "[BOOT] RateLimitStore demarre sur "
                          << seastar::smp::count << " shards\n";
            }
        }

        // ─────────────────────────────────────────────────────
        // 9. Repository + services runtime
        // ─────────────────────────────────────────────────────
        sea::infrastructure::persistence::RepositoryFactory repository_factory;

        auto repository = repository_factory.create(
            service.database_config,
            registry,
            resources,
            blocking_executor
            );

        auto runtime_validator =
            std::make_shared<sea::infrastructure::runtime::GenericValidator>();

        auto crud_engine =
            std::make_shared<sea::infrastructure::runtime::GenericCrudEngine>(
                registry,
                runtime_validator,
                repository
                );

        // ─────────────────────────────────────────────────────
        // 10. Routes + OpenAPI
        // ─────────────────────────────────────────────────────
        sea::application::RouteGenerator route_generator;
        const auto route_definitions = route_generator.generate(service);

        sea::application::OpenApiGenerator openapi_generator;
        const auto openapi_doc =
            openapi_generator.generate(service, route_definitions);

        const auto openapi_json = openapi_doc.dump(2);

        if (is_main_shard()) {
            sea::http::routing::log_route_definitions(
                service.name,
                route_definitions
                );
        }

        // ─────────────────────────────────────────────────────
        // 11. AuthService
        // ─────────────────────────────────────────────────────
        std::shared_ptr<sea::application::AuthService> auth_service = nullptr;

        const bool auth_enabled =
            service.security.authentication().type() !=
            sea::domain::security::AuthType::None;

        if (auth_enabled) {
            auto effective_auth_cfg = service.security.authentication();

            if (!effective_auth_cfg.jwt_secret().empty()) {
                if (effective_auth_cfg.jwt_secret().size() < 32) {
                    throw std::runtime_error(
                        "Le JWT secret du YAML doit faire au moins 32 caracteres"
                        );
                }
            } else {
                sea::infrastructure::security::JwtSecretConfig secret_cfg;
                secret_cfg.storageDir = "./runtime/secrets";
                secret_cfg.serviceName = service.name;

                const auto jwt_secret =
                    sea::infrastructure::security::resolve_jwt_secret(secret_cfg);

                effective_auth_cfg.set_jwt_secret(jwt_secret);
            }

            auth_service =
                std::make_shared<sea::application::AuthService>(
                    effective_auth_cfg,
                    service.name
                    );

            if (is_main_shard()) {
                std::cerr << "[BOOT] Auth activee:"
                          << " type=" << to_string(effective_auth_cfg.type())
                          << " algorithm=" << to_string(effective_auth_cfg.jwt_algorithm())
                          << " access_ttl="
                          << effective_auth_cfg.access_token_ttl().count() << "s"
                          << " refresh_ttl="
                          << effective_auth_cfg.refresh_token_ttl().count() << "s"
                          << "\n";
            }
        } else {
            log_boot("[BOOT] Auth desactivee (type=none)");
        }
        // ─────────────────────────────────────────────────────
        // 11.5. PolicyEngine (Module 5 - Authorization)
        // ─────────────────────────────────────────────────────
        std::shared_ptr<sea::application::access_control::PolicyEngine>
            policy_engine = nullptr;

        if (service.access_control.enabled()) {
            // Le OperatorRegistry contient toutes les strategies d'evaluation
            // (equals, intersects, in, contains, etc.) initialisees au boot.
            static const auto operator_registry =
                sea::application::access_control::OperatorRegistry::create_default();

            policy_engine =
                std::make_shared<sea::application::access_control::PolicyEngine>(
                    operator_registry
                    );

            if (is_main_shard()) {
                std::cerr << "[BOOT] Authorization activee:"
                          << " default_policy="
                          << to_string(service.access_control.default_policy())
                          << " admin_role=" << service.access_control.admin_role()
                          << " abac_mode="
                          << to_string(service.access_control.abac_mode())
                          << "\n";
            }
        } else {
            log_boot("[BOOT] Authorization desactivee");
        }

        // Section 11.6 (après PolicyEngine)
        std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper>
            resource_auth_helper = nullptr;

        if (service.access_control.enabled() && policy_engine) {
            resource_auth_helper = std::make_shared<sea::http::handlers::access_control::ResourceAuthorizationHelper>(
                                       policy_engine,
                                       &service.schema,
                                       &service.access_control
                                       );
        }

        // ─────────────────────────────────────────────────────
        // 12. Auth source
        // ─────────────────────────────────────────────────────
        bool has_auth_source = false;

        for (const auto& e : service.schema.entities) {
            if (e.options.is_auth_source) {
                has_auth_source = true;
                break;
            }
        }

        // ─────────────────────────────────────────────────────
        // 13. MiddlewareContext
        // ─────────────────────────────────────────────────────
        /**
         * Point critique :
         * blocking_executor doit être présent ici.
         *
         * Sinon ProtectedHandler / AuthMiddleware ne peut pas utiliser
         * verify_token_async(...) et risque de refaire du crypto dans le reactor.
         */
        sea::http::routing::MiddlewareContext mw_context{
            .service = service,
            .auth_service = auth_service,
            .rate_limit_store = rate_limits_enabled
                                    ? rate_limit_store.get()
                                    : nullptr,
            .blocking_executor = blocking_executor,
            .policy_engine = policy_engine,
            .resource_auth_helper = resource_auth_helper
        };

        // ─────────────────────────────────────────────────────
        // 14. Serveur HTTP
        // ─────────────────────────────────────────────────────
        auto server =
            std::make_shared<seastar::httpd::http_server_control>();

        try {
            co_await server->start();

            co_await server->server().invoke_on_all([](auto& s) {
                s.set_content_streaming(true);
            });

            co_await server->set_routes([
                                            crud_engine,
                                            registry,
                                            route_definitions,
                                            service,
                                            openapi_json,
                                            auth_service,
                                            auth_enabled,
                                            has_auth_source,
                                            mw_context,
                                            blocking_executor
            ](seastar::httpd::routes& r) {
                using namespace sea::http::routing;

                // ─────────────────────────────────────────────
                // Routes publiques système
                // ─────────────────────────────────────────────
                r.add(
                    seastar::httpd::operation_type::GET,
                    seastar::httpd::url("/health"),
                    wrap_with_middlewares(
                        std::make_unique<sea::http::handlers::misc::HealthHandler>(),
                        false,
                        mw_context
                        ).release()
                    );

                r.add(
                    seastar::httpd::operation_type::GET,
                    seastar::httpd::url("/openapi.json"),
                    wrap_with_middlewares(
                        std::make_unique<sea::http::handlers::misc::OpenApiHandler>(
                            openapi_json
                            ),
                        false,
                        mw_context
                        ).release()
                    );

                r.add(
                    seastar::httpd::operation_type::GET,
                    seastar::httpd::url("/docs"),
                    wrap_with_middlewares(
                        std::make_unique<sea::http::handlers::misc::SwaggerUiHandler>(),
                        false,
                        mw_context
                        ).release()
                    );
                {
                auto register_asset_route = [&](const std::string& path) {
                    r.add(
                        seastar::httpd::operation_type::GET,
                        seastar::httpd::url(path),
                        wrap_with_middlewares(
                            std::make_unique<sea::http::handlers::misc::SwaggerAssetsHandler>(),
                            false,  // pas d'auth
                            mw_context
                            ).release()
                        );
                };

                register_asset_route("/assets/swagger-ui/swagger-ui.css");
                register_asset_route("/assets/swagger-ui/swagger-ui-bundle.js");
                register_asset_route("/assets/swagger-ui/swagger-ui-standalone-preset.js");
                register_asset_route("/assets/swagger-ui/favicon-32x32.png");
                }
                // ─────────────────────────────────────────────
                // Routes auth
                // ─────────────────────────────────────────────
                if (auth_enabled && auth_service && has_auth_source) {
                    r.add(
                        seastar::httpd::operation_type::POST,
                        seastar::httpd::url("/auth/register"),
                        wrap_with_middlewares(
                            std::make_unique<sea::http::handlers::auth::RegisterHandler>(
                                crud_engine,
                                registry,
                                auth_service,
                                blocking_executor,
                                service.database_config.type
                                ),
                            false,
                            mw_context
                            ).release()
                        );

                    r.add(
                        seastar::httpd::operation_type::POST,
                        seastar::httpd::url("/auth/login"),
                        wrap_with_middlewares(
                            std::make_unique<sea::http::handlers::auth::LoginHandler>(
                                crud_engine,
                                auth_service,
                                blocking_executor
                                ),
                            false,
                            mw_context
                            ).release()
                        );

                    r.add(
                        seastar::httpd::operation_type::GET,
                        seastar::httpd::url("/auth/me"),
                        wrap_with_middlewares(
                            std::make_unique<sea::http::handlers::auth::MeHandler>(
                                crud_engine
                                ),
                            true,
                            mw_context
                            ).release()
                        );
                }

                // ─────────────────────────────────────────────
                // Routes CRUD collection
                // ─────────────────────────────────────────────
                for (const auto& route : route_definitions) {
                    if (route.operation_name == "list" ||
                        route.operation_name == "create") {
                        register_collection_route(
                            r,
                            route,
                            crud_engine,
                            registry,
                            mw_context
                            );
                    }
                }

                // ─────────────────────────────────────────────
                // Routes relationnelles
                // ─────────────────────────────────────────────
                register_has_many_routes(r, crud_engine, mw_context);
                register_has_one_routes(r, crud_engine, mw_context);
                register_many_to_many_routes(r, crud_engine, mw_context);

                // ─────────────────────────────────────────────
                // Routes CRUD item
                // ─────────────────────────────────────────────
                for (const auto& route : route_definitions) {
                    if (route.operation_name == "get_by_id" ||
                        route.operation_name == "update" ||
                        route.operation_name == "delete") {
                        register_item_route(
                            r,
                            route,
                            crud_engine,
                            registry,
                            mw_context
                            );
                    }
                }


            });

            if (is_main_shard()) {
                std::cerr << "[BOOT] Serveur en ecoute sur le port "
                          << service.port << "\n";
            }

            co_await server->listen(seastar::ipv4_addr{service.port});

            /**
             * Maintient le serveur vivant.
             */
            co_await seastar::sleep(std::chrono::hours(24 * 365));

        } catch (...) {
            try {
                throw;
            } catch (const std::exception& e) {
                std::cerr << "[ERROR] " << e.what() << "\n";
            } catch (...) {
                std::cerr << "[ERROR] Exception inconnue\n";
            }
        }

        // ─────────────────────────────────────────────────────
        // 15. Cleanup
        // ─────────────────────────────────────────────────────
        co_await server->stop();

        if (service.database_config.type == sea::domain::DatabaseType::MySQL) {
            co_await mysql_pool->stop();
        }

        if (rate_limits_enabled) {
            co_await rate_limit_store->stop();
        }

        co_return;
    });
}