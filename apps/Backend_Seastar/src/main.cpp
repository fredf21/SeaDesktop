#include <seastar/core/app-template.hh>
#include <seastar/core/sleep.hh>
#include <seastar/core/sharded.hh>
#include <seastar/core/when_all.hh>
#include <seastar/core/reactor.hh>
#include <seastar/http/httpd.hh>

#include <boost/program_options.hpp>

#include <chrono>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

#include "authservice.h"
#include "fileservice.h"
#include "fileservicefactory.h"
#include "http/handlers/file_handlers/file_upload_extractor.h"
#include "http/handlers/auth_handlers/logout_handler.h"
#include "http/handlers/auth_handlers/refresh_handler.h"
#include "http/handlers/logs_handlers/logs_handler.h"
#include "http/routing/pagination_routes.h"
#include "import_yaml_schema_usecase.h"
#include "openapigenerator.h"
#include "persistence/mysql/seed_orchestrator.h"
#include "route_generator.h"
#include "spdlog/spdlog.h"
#include "token_tracking_service.h"
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
#include "persistence/mysql/mysql_bootstrapper.h"

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
#include "logging_initializer.h"

namespace bpo = boost::program_options;

namespace {

bool is_main_shard()
{
    return seastar::this_shard_id() == 0;
}

void log_boot(const std::string& message)
{

    spdlog::get("sea.boot")->info("{}", message);

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

        auto service = *selected_service;
        // ─────────────────────────────────────────────────────
        // 2bis. Initialisation du logging (Etape 2.3 Sujet 2)
        // ─────────────────────────────────────────────────────
        // Initialise spdlog avec les sinks/niveaux/format declares dans
        // le YAML, puis installe le hook Seastar -> spdlog pour que les
        // logs internes Seastar passent par notre infra.
        //
        // ATTENTION : cet appel doit precedeer tout autre log [BOOT]
        // (sinon les premiers std::cerr passent encore en stderr brut).
        //
        // En cas d'erreur de config logging, on tombe sur stderr classique
        // pour ne pas bloquer le boot du service.
        try {
            sea::application::logging::LoggingInitializer::init(service.logging);
            if (is_main_shard()) {
                std::cerr << "[BOOT] Logging initialise (niveau="
                          << to_string(service.logging.level()) << ")\n";
            }
        } catch (const std::exception& e) {
            std::cerr << "[BOOT] WARNING: init logging echoue: " << e.what()
            << " -- fallback sur stderr\n";
        }

        // Après l'import du service depuis YAML
        const auto& ac_config = service.access_control;

        spdlog::get("sea.boot")->info(
            "Authorization: {}",
            ac_config.enabled() ? "ENABLED" : "DISABLED"
            );


        if (ac_config.enabled()) {
            using namespace sea::domain::access_control;

            spdlog::get("sea.boot")->info(
                "  default_policy: {}", to_string(ac_config.default_policy())
                );
            spdlog::get("sea.boot")->info(
                "  admin_role: {}", ac_config.admin_role()
                );
            spdlog::get("sea.boot")->info(
                "  default_scope_field: {}", ac_config.default_scope_field()
                );
            std::string roles_str;
            for (const auto& r : ac_config.declared_roles()) {
                if (!roles_str.empty()) roles_str += " ";
                roles_str += r;
            }
            spdlog::get("sea.boot")->info(
                "  declared_roles: {}", roles_str
                );

            // Per-entity rules
            spdlog::get("sea.boot")->info("Per-entity access control rules:");
            for (const auto& entity : service.schema.entities) {
                const auto& entity_ac = entity.access_control;
                if (!entity_ac.has_any_spec()) continue;

                std::string entity_line = "  " + entity.name;
                if (!entity_ac.scope_field().empty()) {
                    entity_line += " (scope_field=" + entity_ac.scope_field() + ")";
                }
                if (!entity_ac.owner_field().empty()) {
                    entity_line += " (owner_field=" + entity_ac.owner_field() + ")";
                }
                spdlog::get("sea.boot")->info("{}", entity_line);

                for (int op_idx = 0; op_idx <= 4; ++op_idx) {
                    const auto op = static_cast<CrudOperation>(op_idx);
                    const auto* spec = entity_ac.find_spec(op);
                    if (spec && !spec->is_empty()) {
                        spdlog::get("sea.boot")->info(
                            "    {} {}",
                            to_string(op),
                            spec->requires_resource()
                                ? "(resource-aware)"
                                : "(subject-only, fast path)"
                            );

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

        //--------------------------------------------------------
        // 4.1 verifier si le token tracking est actif et injecter dans le schema avant le bootstrap
        //------------------------------------------------------------------------------------------
        // ─── Entités SYSTÈME : tables de token tracking ──────────────
        // Si le token tracking est actif, ses tables (RefreshToken,
        // RevokedToken) doivent exister en base ET être connues du
        // registry — car TokenTrackingService les manipule via
        // repository->create(...) et find_one_by_field(...).
        //
        // On les injecte dans le schéma AVANT le bootstrap : le
        // MysqlBootstrapper les créera comme des tables normales, et
        // register_schema(service.schema) les enregistrera dans le
        // registry au passage. Pas de mécanisme dédié nécessaire.
        //
        // Symétrique de ce qui est fait pour sea_files (qui, lui, a une
        // DDL spéciale BINARY(16) et passe par ensure_sea_files_table).
        if (service.security.authentication().token_tracking().is_enabled()) {
            const auto& tt = service.security.authentication().token_tracking();

            auto make_field = [](const std::string& name,
                                 sea::domain::FieldType type,
                                 bool required) {
                sea::domain::Field f;
                f.name     = name;
                f.type     = type;
                f.required = required;
                return f;
            };

            // RefreshToken — allowlist des refresh tokens actifs.
            // Colonnes utilisées par TokenTrackingService::register_refresh
            // et is_refresh_valid (recherche par jti).
            sea::domain::Entity refresh_token;
            refresh_token.name       = tt.refresh_table();   // "RefreshToken"
            refresh_token.table_name = tt.refresh_table();
            refresh_token.options.enable_crud = false;
            refresh_token.options.timestamps  = false;
            refresh_token.fields = {
                make_field("id",                sea::domain::FieldType::UUID,      true),
                make_field("jti",               sea::domain::FieldType::String,    true),
                make_field("user_id",           sea::domain::FieldType::String,    true),
                make_field("issued_at",         sea::domain::FieldType::Timestamp, true),
                make_field("expires_at",        sea::domain::FieldType::Timestamp, true),
                make_field("revoked_at",        sea::domain::FieldType::Timestamp, false),
                make_field("replaced_by_jti",   sea::domain::FieldType::String,    false),
                make_field("device_info",       sea::domain::FieldType::String,    false),
                make_field("ip_address",        sea::domain::FieldType::String,    false),
            };

            // RevokedToken — denylist des access tokens révoqués.
            // Colonnes utilisées par revoke_access et is_access_revoked.
            sea::domain::Entity revoked_token;
            revoked_token.name       = tt.revoked_table();   // "RevokedToken"
            revoked_token.table_name = tt.revoked_table();
            revoked_token.options.enable_crud = false;
            revoked_token.options.timestamps  = false;
            revoked_token.fields = {
                make_field("id",         sea::domain::FieldType::UUID,      true),
                make_field("jti",        sea::domain::FieldType::String,    true),
                make_field("user_id",    sea::domain::FieldType::String,    true),
                make_field("revoked_at", sea::domain::FieldType::Timestamp, true),
                make_field("expires_at", sea::domain::FieldType::Timestamp, true),
                make_field("reason",     sea::domain::FieldType::String,    false),
            };

            service.schema.entities.push_back(refresh_token);
            service.schema.entities.push_back(revoked_token);

            spdlog::get("sea.boot")->info(
                "main: token tracking enabled — injected system entities '{}' and '{}' "
                "into schema (will be bootstrapped + registered)",
                tt.refresh_table(), tt.revoked_table());
        }
        // ─────────────────────────────────────────────────────
        // 5. Registry runtime
        // ─────────────────────────────────────────────────────
        auto registry =
            std::make_shared<sea::infrastructure::runtime::SchemaRuntimeRegistry>();

        registry->register_schema(service.schema);
        // ── Entité SYSTÈME : sea_files ────────────────────────────
        // Table interne créée par le bootstrapper quand le schéma a
        // des champs File. Pas déclarée dans le YAML utilisateur,
        // donc doit être enregistrée explicitement dans le registry
        // pour que FileRepository::insert (qui appelle create("sea_files",..))
        // la résolve. Sans ça : INSERT silencieusement perdu, upload échoue.
        if (service.schema.has_file_fields()) {
            sea::domain::Entity sea_files;
            sea_files.name       = "sea_files";
            sea_files.table_name = "sea_files";
            sea_files.fields = {
                                []{ sea::domain::Field f; f.name="id";              f.type=sea::domain::FieldType::UUID;      f.required=true;  return f; }(),
                                []{ sea::domain::Field f; f.name="original_name";   f.type=sea::domain::FieldType::String;    f.required=true;  return f; }(),
                                []{ sea::domain::Field f; f.name="mime_type";       f.type=sea::domain::FieldType::String;    f.required=true;  return f; }(),
                                []{ sea::domain::Field f; f.name="size_bytes";      f.type=sea::domain::FieldType::BigInt;    f.required=true;  return f; }(),
                                []{ sea::domain::Field f; f.name="storage_path";    f.type=sea::domain::FieldType::String;    f.required=true;  return f; }(),
                                []{ sea::domain::Field f; f.name="reference_count"; f.type=sea::domain::FieldType::Int;       f.required=true;  return f; }(),
                                []{ sea::domain::Field f; f.name="created_at";      f.type=sea::domain::FieldType::Timestamp; f.required=false; return f; }(),
                                };
            registry->register_entity(sea_files);
            spdlog::get("sea.boot")->info(
                "main: system entity 'sea_files' registered in runtime registry");
        }
        // ─── Tables pivots M2M ───────────────────────────────
        // Les tables pivots (project_tags, etc.) sont creees par le
        // bootstrapper a partir des relations many_to_many declarees
        // dans le YAML, mais ne sont pas des entites du schema
        // utilisateur. On les enregistre ici comme entites minimales
        // pour que ListManyToManyHandler puisse les lire via
        // crud_engine_->list(pivot_table) (sinon retourne {} silencieusement).
        //
        // enable_crud = false : on ne veut PAS exposer les pivots en
        // CRUD public — sinon faille de securite identique a celle
        // de RefreshToken/RevokedToken (bug 6).
        std::unordered_set<std::string> registered_pivots;
        for (const auto& entity : service.schema.entities) {
            for (const auto& relation : entity.relations) {
                if (relation.kind != sea::domain::RelationKind::ManyToMany) continue;
                if (relation.pivot_table.empty()) continue;
                if (registered_pivots.count(relation.pivot_table)) continue;
                registered_pivots.insert(relation.pivot_table);

                sea::domain::Entity pivot;
                pivot.name       = relation.pivot_table;
                pivot.table_name = relation.pivot_table;
                pivot.options.enable_crud = false;
                pivot.options.timestamps  = false;
                pivot.fields = {
                                []() { sea::domain::Field f; f.type = sea::domain::FieldType::UUID; f.required = true; return f; }(),
                                []() { sea::domain::Field f; f.type = sea::domain::FieldType::UUID; f.required = true; return f; }(),
                                };
                pivot.fields[0].name = relation.source_fk_column;
                pivot.fields[1].name = relation.target_fk_column;

                registry->register_entity(pivot);
                spdlog::get("sea.boot")->info(
                    "main: pivot table '{}' registered in runtime registry "
                    "(source_fk={}, target_fk={})",
                    relation.pivot_table,
                    relation.source_fk_column,
                    relation.target_fk_column);
            }
        }
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
            std::make_shared<StdThreadPoolExecutor>(16);

        // ─────────────────────────────────────────────────────
        // 7. Pool MySQL + Bootstrap (Phase A)
        // ─────────────────────────────────────────────────────
        auto mysql_pool =
            std::make_shared<
                seastar::sharded<
                    sea::infrastructure::persistence::mysql::MysqlConnexionPool
                    >
                >();

        sea::infrastructure::persistence::RepositoryFactory::DatabaseResources resources;

        if (service.database_config.type == sea::domain::DatabaseType::MySQL) {

            // Ensure database exists (AVANT le pool)
            //
            // Le pool ne peut pas demarrer si la database n'existe pas.
            // On utilise une connexion ad-hoc (sans database_name) pour
            // executer CREATE DATABASE IF NOT EXISTS.
            if (service.database_config.migrations.enabled
                && service.database_config.migrations.create_database_if_missing) {

                spdlog::get("sea.boot")->info(
                    "═══ PHASE A : ensure database exists ═══"
                    );


                sea::infrastructure::persistence::mysql::MysqlBootstrapper db_bootstrapper(
                    service.database_config,
                    service.schema,
                    *mysql_pool,        // pas utilise dans ensure_database_exists()
                    blocking_executor
                    );

                const bool db_ok = co_await db_bootstrapper.ensure_database_exists();
                if (!db_ok) {
                    throw std::runtime_error(
                        "Bootstrap: impossible de creer la database '" +
                        service.database_config.database_name + "'"
                        );
                }
            }

            // ETAPE 8 : Demarrer le pool (la DB existe maintenant)
            sea::infrastructure::persistence::mysql::MySQLConnector connector(
                service.database_config.host,
                service.database_config.username,
                service.database_config.password,
                service.database_config.database_name,
                static_cast<unsigned int>(service.database_config.port)
                );

            constexpr std::size_t pool_size = 16;

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

            log_boot("MySQL pool demarre");

            // ETAPE 8. : Bootstrap complet (introspect + CREATE TABLE + ADD COLUMN)
            //
            // Maintenant que le pool est demarre, on peut introspect MySQL
            // et appliquer les migrations.
            if (service.database_config.migrations.enabled) {

                spdlog::get("sea.boot")->info(
                    "═══ PHASE A : bootstrap schema (CREATE TABLE / ADD COLUMN) ═══"
                    );


                sea::infrastructure::persistence::mysql::MysqlBootstrapper bootstrapper(
                    service.database_config,
                    service.schema,
                    *mysql_pool,
                    blocking_executor
                    );

                const auto result = co_await bootstrapper.bootstrap();

                if (!result.success) {
                    spdlog::get("sea.boot")->warn(
                        "Bootstrap a echoue avec {} erreur(s)",
                        result.errors.size()
                        );
                    spdlog::get("sea.boot")->warn(
                        "Le serveur va tenter de demarrer quand meme"
                        );

                }
            }
        }

        // ─────────────────────────────────────────────────────
        // 9. RateLimitStore
        // ─────────────────────────────────────────────────────
        auto rate_limit_store =
            std::make_shared<
                seastar::sharded<sea::http::middlewares::RateLimitStore>
                >();

        const bool rate_limits_enabled =
            !service.security.rate_limits().empty();

        if (rate_limits_enabled) {
            co_await rate_limit_store->start();

            spdlog::get("sea.boot")->info(
                "RateLimitStore demarre sur {} shards",
                seastar::smp::count
                );

        }

        // ─────────────────────────────────────────────────────
        // 10. Repository + services runtime
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
        // 10bis. FileService + FileUploadExtractor (conditionnel)
        // ─────────────────────────────────────────────────────
        // Si le schema declare au moins un champ File, on instancie
        // toute la pile fichiers : IFileStorage (Filesystem) +
        // FileRepository + FileService + FileUploadExtractor.
        // Sinon on garde les pointeurs nullptr et les handlers HTTP
        // retombent silencieusement sur leur comportement d'origine
        // (JSON uniquement, pas de gestion de fichiers).
        //
        // Le FileService partage le meme `repository` que le crud_engine
        // : c'est essentiel pour que les transactions englobent INSERT
        // sea_files + INSERT entite dans la meme tx SQL.
        std::shared_ptr<sea::application::FileService> file_service;
        std::shared_ptr<sea::http::handlers::file_upload::FileUploadExtractor> file_extractor;

        if (auto bundle = sea::application::FileServiceFactory::make(
                service, repository, blocking_executor))
        {
            file_service = bundle->file_service;
            // Construit l'extractor cote apps/ (le factory ne le fait pas
            // car FileUploadExtractor vit dans la couche HTTP, au-dessus
            // de sea_application).
            file_extractor = std::make_shared<
                sea::http::handlers::file_upload::FileUploadExtractor>(file_service);
            spdlog::get("sea.boot")->info(
                "main: file service stack ready (storage + repo + service + extractor)");
        }


        // ─────────────────────────────────────────────────────
        // 11. Phase Seeds : insertion des donnees initiales
        // ─────────────────────────────────────────────────────
        if (service.database_config.is_mysql()
            && service.database_config.migrations.seeds.enabled) {

            spdlog::get("sea.boot")->info(
                "═══ PHASE C : seed initial data ═══"
                );


            auto seed_introspector =
                std::make_shared<sea::infrastructure::persistence::mysql::MysqlIntrospector>(
                    *mysql_pool,
                    blocking_executor
                    );

            sea::infrastructure::persistence::mysql::SeedOrchestrator orchestrator(
                service.database_config,
                service.schema,
                crud_engine,
                seed_introspector,
                blocking_executor,      // Pour BCrypt async
                repository
                );

            const auto seed_result = co_await orchestrator.seed_all();

            if (!seed_result.success) {
                if (service.database_config.migrations.seeds.on_error
                    == sea::domain::SeedsErrorPolicy::Abort) {
                    throw std::runtime_error("Seeds failed and on_error=abort");
                }
                spdlog::get("sea.boot")->warn("Seeds had errors, continuing");
            }
        }



        // ─────────────────────────────────────────────────────
        // 12. Routes + OpenAPI
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
        // 13. AuthService
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

            spdlog::get("sea.boot")->info(
                "Auth activee: type={} algorithm={} access_ttl={}s refresh_ttl={}s",
                to_string(effective_auth_cfg.type()),
                to_string(effective_auth_cfg.jwt_algorithm()),
                effective_auth_cfg.access_token_ttl().count(),
                effective_auth_cfg.refresh_token_ttl().count()
                );

        } else {
            log_boot("Auth desactivee (type=none)");
        }
        // ─────────────────────────────────────────────────────
        // 13bis TokenTrackingService
        // ─────────────────────────────────────────────────────
        //
        // Construit UNIQUEMENT si auth_service existe (sinon nullptr).
        //
        // Si token_tracking.enabled = false dans le YAML, le service est cree
        // quand meme mais en mode no-op (toutes ses methodes deviennent des
        // passthroughs). Cela simplifie le code des handlers : ils l'appellent
        // toujours, sans avoir a verifier le mode.
        //
        // Si auth_service == nullptr, token_tracking reste nullptr et les
        // handlers le testent avant utilisation (ils gerent deja ce cas).
        std::shared_ptr<sea::application::auth::TokenTrackingService> token_tracking = nullptr;
        if (auth_service) {
            token_tracking = std::make_shared<sea::application::auth::TokenTrackingService>(
                repository,    // ton IGenericRepository
                auth_service->config().token_tracking()
                );

            const auto& tt_cfg = auth_service->config().token_tracking();

            spdlog::get("sea.boot")->info(
                "TokenTracking {} (refresh_table={}, revoked_table={}, cache_ttl={}s)",
                tt_cfg.is_enabled() ? "active" : "desactive (mode no-op)",
                tt_cfg.refresh_table(),
                tt_cfg.revoked_table(),
                tt_cfg.cache().ttl.count()
                );

        }
        if (token_tracking &&
            auth_service->config().token_tracking().is_enabled() &&
            auth_service->config().token_tracking().auto_cleanup().is_enabled()) {

            const auto interval =
                auth_service->config().token_tracking().auto_cleanup().interval;

            // Timer Seastar declenche periodiquement le cleanup.
            // Reference statique pour qu'il ne soit pas detruit prematurement.
            // En production, mieux : stocker dans la classe Application/ServiceState.
            static seastar::timer<> cleanup_timer;
            cleanup_timer.set_callback([token_tracking]() {
                (void)token_tracking->cleanup_expired().then_wrapped(
                    [](seastar::future<sea::application::auth::TokenTrackingService::CleanupReport> f) {
                        try {
                            auto report = f.get();
                            if (report.refresh_deleted > 0 || report.revoked_deleted > 0) {
                                spdlog::get("sea.security")->info(
                                    "cleanup: refresh={} revoked={}",
                                    report.refresh_deleted,
                                    report.revoked_deleted
                                    );
                            }
                        } catch (const std::exception& e) {
                            spdlog::get("sea.security")->error(
                                "cleanup error: {}", e.what()
                                );
                        }

                    });
            });
            cleanup_timer.arm_periodic(interval);

            spdlog::get("sea.boot")->info(
                "TokenTracking cleanup periodique active (interval={}s)",
                interval.count()
                );

        }

        // ─────────────────────────────────────────────────────
        // 14. PolicyEngine
        // ─────────────────────────────────────────────────────
        std::shared_ptr<sea::domain::access_control::PolicyEngine>
            policy_engine = nullptr;

        if (service.access_control.enabled()) {
            // Le OperatorRegistry contient toutes les strategies d'evaluation
            // (equals, intersects, in, contains, etc.) initialisees au boot.
            static const auto operator_registry =
                sea::domain::access_control::OperatorRegistry::create_default();

            policy_engine =
                std::make_shared<sea::domain::access_control::PolicyEngine>(
                    operator_registry
                    );

            spdlog::get("sea.boot")->info(
                "Authorization activee: default_policy={} admin_role={} abac_mode={}",
                to_string(service.access_control.default_policy()),
                service.access_control.admin_role(),
                to_string(service.access_control.abac_mode())
                );

        } else {
            log_boot("Authorization desactivee");
        }

        // Section 14
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
        // 15. Auth source
        // ─────────────────────────────────────────────────────
        bool has_auth_source = false;

        for (const auto& e : service.schema.entities) {
            if (e.options.is_auth_source) {
                has_auth_source = true;
                break;
            }
        }

        // ─────────────────────────────────────────────────────
        // 16. MiddlewareContext
        // ─────────────────────────────────────────────────────
        /**
         * Point critique :
         * blocking_executor doit être présent ici.
         *
         * Sinon ProtectedHandler / AuthMiddleware ne peut pas utiliser
         * verify_token_async(...) et risque de refaire du crypto dans le reactor.
         */
        sea::http::routing::MiddlewareContext mw_context{
            .service              = service,
            .auth_service         = auth_service,
            .rate_limit_store     = rate_limit_store.get(),
            .blocking_executor    = blocking_executor,
            .policy_engine        = policy_engine,
            .resource_auth_helper = resource_auth_helper,
            // ── AJOUT token tracking ──
            .token_tracking       = token_tracking,
            .cookie_config        = auth_service
                                 ? auth_service->config().cookie_config()
                                 : sea::domain::security::CookieConfig{},
            // ── Pile fichiers (nullptr si le schema n'a pas de champ File) ──
            .file_extractor       = file_extractor,
            .file_service         = file_service
        };

        // ─────────────────────────────────────────────────────
        // 17. Serveur HTTP
        // ─────────────────────────────────────────────────────
        auto server =
            std::make_shared<seastar::httpd::http_server_control>();
        std::exception_ptr boot_error;
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
                                            , token_tracking](seastar::httpd::routes& r) {

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

                    // Recuperation des configs depuis auth_service (etape 1.4)
                    const auto& auth_cfg    = auth_service->config();
                    const auto& cookie_cfg  = auth_cfg.cookie_config();
                    const auto  delivery    = auth_cfg.token_delivery();
                    const auto  access_ttl  = auth_cfg.access_token_ttl();
                    const auto  refresh_ttl = auth_cfg.refresh_token_ttl();

                    // ─── POST /auth/register (inchange) ────────────────────────
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

                    // ─── POST /auth/login (ENRICHI etape 1.4) ──────────────────
                    r.add(
                        seastar::httpd::operation_type::POST,
                        seastar::httpd::url("/auth/login"),
                        wrap_with_middlewares(
                            std::make_unique<sea::http::handlers::auth::LoginHandler>(
                                crud_engine,
                                auth_service,
                                token_tracking,        // ← AJOUT
                                blocking_executor,
                                cookie_cfg,            // ← AJOUT
                                delivery,              // ← AJOUT
                                access_ttl,            // ← AJOUT
                                refresh_ttl            // ← AJOUT
                                ),
                            false,
                            mw_context
                            ).release()
                        );

                    // ─── POST /auth/refresh (NOUVEAU etape 1.4) ────────────────
                    // requires_auth = false : l'utilisateur n'a plus son access_token
                    // valide (c'est pour ca qu'il fait /refresh), donc le middleware
                    // de protection ne doit PAS verifier l'access. La validation se
                    // fait via le refresh_token (allowlist) DANS le handler.
                    r.add(
                        seastar::httpd::operation_type::POST,
                        seastar::httpd::url("/auth/refresh"),
                        wrap_with_middlewares(
                            std::make_unique<sea::http::handlers::auth::RefreshHandler>(
                                crud_engine,
                                auth_service,
                                token_tracking,
                                blocking_executor,
                                cookie_cfg,
                                delivery,
                                access_ttl,
                                refresh_ttl
                                ),
                            false,
                            mw_context
                            ).release()
                        );

                    // ─── POST /auth/logout (NOUVEAU etape 1.4) ─────────────────
                    // requires_auth = true : pour se deconnecter, il faut etre
                    // identifie (c'est ProtectedHandler qui posera les X-User-*).
                    // Le LogoutHandler revoque ensuite les tokens et clear les cookies.
                    r.add(
                        seastar::httpd::operation_type::POST,
                        seastar::httpd::url("/auth/logout"),
                        wrap_with_middlewares(
                            std::make_unique<sea::http::handlers::auth::LogoutHandler>(
                                auth_service,
                                token_tracking,
                                blocking_executor,
                                cookie_cfg
                                ),
                            true,
                            mw_context
                            ).release()
                        );

                    // ─── GET /auth/me (inchange) ───────────────────────────────
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
                // Routes /admin/logs (etape 2.5)
                //
                // Securite a 2 couches :
                //   1. ProtectedHandler (auth requise) via wrap_with_middlewares
                //   2. Garde admin role dans le handler (compare X-User-Role
                //      avec service.access_control.admin_role())
                //
                // GET /admin/logs              : lit le ring buffer
                // GET /admin/logs/loggers      : liste des loggers connus
                // ─────────────────────────────────────────────
                {
                    auto ring_buffer_sink =
                        sea::application::logging::LoggingInitializer::get_ring_buffer_sink();

                    // Recupere le role admin configure dans le YAML
                    // (authorization.admin_role, default "admin")
                    const std::string admin_role_name = service.access_control.admin_role();

                    if (ring_buffer_sink) {
                        r.add(
                            seastar::httpd::operation_type::GET,
                            seastar::httpd::url("/admin/logs"),
                            wrap_with_middlewares(
                                std::make_unique<sea::http::handlers::logs::admin::LogsHandler>(
                                    ring_buffer_sink,
                                    admin_role_name
                                    ),
                                true,            // requires_auth (ProtectedHandler)
                                mw_context
                                ).release()
                            );

                        r.add(
                            seastar::httpd::operation_type::GET,
                            seastar::httpd::url("/admin/logs/loggers"),
                            wrap_with_middlewares(
                                std::make_unique<sea::http::handlers::logs::admin::LoggersListHandler>(
                                    admin_role_name
                                    ),
                                true,            // requires_auth
                                mw_context
                                ).release()
                            );
                    }
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
                register_file_download_routes(r, crud_engine, registry, mw_context);

                // Routes paginées (etape 5)
                //
                // Lit toutes les RouteDefinition se terminant par _page, _offset, _cursor
                // et les enregistre via match_rule (necessaire pour /.../{id}/page).
                // Si aucune entité du YAML n'active la pagination, cet appel est un no-op.
                register_pagination_routes(r, route_definitions, crud_engine, mw_context);


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

            spdlog::get("sea.boot")->info(
                "Serveur en ecoute sur le port {}",
                service.port
                );


            co_await server->listen(seastar::ipv4_addr{service.port});

            /**
             * Maintient le serveur vivant.
             */
            co_await seastar::sleep(std::chrono::hours(24 * 365));

        } catch (...) {
            try {
                throw;
            } catch (const std::exception& e) {
                spdlog::get("sea.boot")->error("Server error: {}", e.what());
            } catch (...) {
                spdlog::get("sea.boot")->error("Unknown exception during server lifecycle");
            }

        }

        // ─────────────────────────────────────────────────────
        // 18. Cleanup garanti — execute toujours, meme apres exception
        // ─────────────────────────────────────────────────────
        try {
            co_await server->stop();
        } catch (const std::exception& e) {
            spdlog::get("sea.boot")->error("server->stop() failed: {}", e.what());
        }

        if (service.database_config.type == sea::domain::DatabaseType::MySQL) {
            try {
                co_await mysql_pool->stop();
            } catch (const std::exception& e) {
                spdlog::get("sea.boot")->error("mysql_pool->stop() failed: {}", e.what());
            }
        }

        if (rate_limits_enabled) {
            try {
                co_await rate_limit_store->stop();
            } catch (const std::exception& e) {
                spdlog::get("sea.boot")->error("rate_limit_store->stop() failed: {}", e.what());
            }
        }

        if (boot_error) {
            std::rethrow_exception(boot_error);
        }
        co_return;
    });
}