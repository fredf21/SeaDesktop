#ifndef ROUTE_REGISTRATION_H
#define ROUTE_REGISTRATION_H

#include "http/handlers/access_control/resource_authorization_helper.h"
#include "http/handlers/misc_handlers/preflight_handler.h"
#include "http/routing/paginated_match_rule.h"
#include "route_generator.h"
#include "service.h"
#include "../middlewares/rate_limit_store.h"

#include <seastar/core/sharded.hh>
#include <seastar/http/httpd.hh>

#include <memory>
#include <string>
#include <vector>
#include "spdlog/spdlog.h"
#include "thread_pool_execution/i_blocking_executor.h"

// forward-declare PolicyEngine
namespace sea::domain::access_control {
class PolicyEngine;
}

namespace sea::infrastructure::runtime {
class GenericCrudEngine;
class SchemaRuntimeRegistry;
}

namespace sea::application {
class AuthService;
}
namespace sea::application::auth {
class TokenTrackingService;
}

// Forward declaration pour la prise en charge des champs File.
// Le file_extractor est optionnel dans le MiddlewareContext : si nullptr
// (cas des services sans champ File ou en attendant le branchement
// complet du FileService), les handlers fonctionnent en mode JSON pur.
namespace sea::http::handlers::file_upload {
class FileUploadExtractor;
}

// Forward declaration du FileService, utilisé par les handlers de
// download (GET /<entity>/{id}/<field>) qui doivent récupérer
// metadata + contenu binaire depuis sea_files + IFileStorage.
namespace sea::application {
class FileService;
}

namespace sea::http::routing {

// Contexte regroupant tout ce dont les middlewares ont besoin
struct MiddlewareContext {
    const sea::domain::Service& service;
    std::shared_ptr<sea::application::AuthService> auth_service;
    seastar::sharded<sea::http::middlewares::RateLimitStore>* rate_limit_store;
    std::shared_ptr<IBlockingExecutor> blocking_executor;

    // PolicyEngine pour AuthorizationMiddleware
    std::shared_ptr<sea::domain::access_control::PolicyEngine> policy_engine;
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> resource_auth_helper;

    // ───────────────────────────────────────
    // TokenTrackingService pour la verification denylist des access tokens
    // et le tracking des refresh tokens.
    // Peut etre nullptr si auth_service est null ou si token_tracking
    // est desactive dans le YAML.
    std::shared_ptr<sea::application::auth::TokenTrackingService> token_tracking;

    // Configuration des cookies (path, secure, same_site, names, etc.).
    // Utilise par ProtectedHandler pour lire le cookie sea_access en
    // fallback de l'header Authorization.
    // Si auth est desactive, cette config a ses defauts (utilise mais inerte).
    sea::domain::security::CookieConfig cookie_config;

    // ───────────────────────────────────────
    // FileUploadExtractor : injecté dans les handlers Create/Update/Delete
    // pour permettre la prise en charge des champs File (upload multipart
    // ou JSON+base64, release des UUIDs au delete, etc.).
    //
    // Peut etre nullptr si :
    //   - le YAML ne declare aucune entite avec un champ File,
    //   - le FileService n'a pas pu etre construit au boot (storage indispo).
    //
    // Quand nullptr, les handlers retombent silencieusement sur leur
    // comportement d'origine (JSON uniquement, pas de gestion de fichiers).
    std::shared_ptr<sea::http::handlers::file_upload::FileUploadExtractor> file_extractor;

    // FileService : utilisé par les handlers de download (GET sur
    // /<entity>/{id}/<field>). Peut etre nullptr pour les mêmes
    // raisons que file_extractor. Si nullptr, les routes de download
    // ne sont tout simplement pas enregistrées.
    std::shared_ptr<sea::application::FileService> file_service;
};

// Wrap un handler avec toute la stack de middlewares.
// Utilise context.service.security pour les configs.
std::unique_ptr<seastar::httpd::handler_base> wrap_with_middlewares(
    std::unique_ptr<seastar::httpd::handler_base> handler,
    bool requires_auth,
    const MiddlewareContext& context
    );
// ─────────────────────────────────────────────────────────────────
// register_options_route
//
// Enregistre une route OPTIONS pour gérer les preflight CORS.
// À appeler en parallèle de chaque route CRUD/relations/M2M.
//
// Le PreflightHandler est trivial (renvoie 204) — le vrai travail
// est fait par CorsMiddleware::handle_preflight() qui s'exécute
// en amont via wrap_with_middlewares.
// ─────────────────────────────────────────────────────────────────
inline void register_options_route(
    seastar::httpd::routes& routes,
    const std::string& path,
    const MiddlewareContext& context)
{
    // Set local à la fonction d'enregistrement complète. On
    // l'initialise une fois et le réutilise pour skip les
    // doublons. Avec une variable statique locale, ça marche
    // pour un seul appel à register_routes, ce qui est suffisant
    // pour le démarrage du serveur (le seul moment où ces
    // fonctions sont appelées).
    //
    // Note : si on doit gérer plusieurs services dans un même
    // serveur, il faudra repenser ça. Pour le MVP, OK.
    static std::unordered_set<std::string> registered_paths;

    if (registered_paths.count(path)) {
        return;  // déjà enregistré
    }
    registered_paths.insert(path);

    spdlog::get("sea.boot")->info(
        "[ROUTE] OPTIONS {} -> PreflightHandler (CORS)",
        path
        );

    auto preflight = std::make_unique<sea::http::handlers::misc::PreflightHandler>();
    auto wrapped = wrap_with_middlewares(
        std::move(preflight),
        false,  // requires_auth = false (OPTIONS jamais authentifié)
        context
        );

    auto* rule = build_match_rule_from_template(
        path,
        wrapped.release()
        );
    routes.add(rule, seastar::httpd::operation_type::OPTIONS);
}
// Routes CRUD
void register_collection_route(
    seastar::httpd::routes& routes,
    const sea::application::RouteDefinition& route,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry>& registry,
    const MiddlewareContext& context
    );

void register_item_route(
    seastar::httpd::routes& routes,
    const sea::application::RouteDefinition& route,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry>& registry,
    const MiddlewareContext& context
    );

// Routes relationnelles
void register_has_many_routes(
    seastar::httpd::routes& routes,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const MiddlewareContext& context
    );

void register_has_one_routes(
    seastar::httpd::routes& routes,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const MiddlewareContext& context
    );

void register_many_to_many_routes(
    seastar::httpd::routes& routes,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const MiddlewareContext& context
    );

// ─────────────────────────────────────────────
// Routes de download de fichiers
//
// Pour chaque entité du schema qui a un ou plusieurs champ(s) File,
// génère une route GET /<entity_lower>s/{id}/<field>.
//
// Exemples produits :
//   - User a avatar (File) → GET /users/{id}/avatar
//   - Document a pdf (File) → GET /documents/{id}/pdf
//
// L'ABAC est héritée de l'entité parente (Read). Pas de route
// /files/{uuid} système (cf. décisions de design Étape 7.5).
//
// Skip silencieusement si context.file_service est null (pas de
// support fichiers) ou si l'entité n'a aucun champ File.
// ─────────────────────────────────────────────
void register_file_download_routes(
    seastar::httpd::routes& routes,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry>& registry,
    const MiddlewareContext& context
    );

// Logging
void log_route_definitions(
    const std::string& service_name,
    const std::vector<sea::application::RouteDefinition>& route_definitions
    );

} // namespace sea::http::routing

#endif // ROUTE_REGISTRATION_H