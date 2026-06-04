#include "route_registration.h"

#include "../handlers/auth_handlers/protected_handler.h"
#include "../handlers/crud_handlers/create_handler.h"
#include "../handlers/crud_handlers/delete_handler.h"
#include "../handlers/crud_handlers/get_by_id_handler.h"
#include "../handlers/crud_handlers/list_handler.h"
#include "../handlers/crud_handlers/update_handler.h"
#include "../handlers/file_handlers/file_download_by_field_handler.h"
#include "../handlers/relation_handlers/get_one_by_fk_handler.h"
#include "../handlers/relation_handlers/get_with_children_handler.h"
#include "../handlers/relation_handlers/list_by_fk_handler.h"
#include "../handlers/relation_handlers/list_many_to_many_handler.h"
#include "../middlewares/cors_middleware.h"
#include "../middlewares/http_limits_middleware.h"
#include "../middlewares/rate_limit_middleware.h"
#include "../middlewares/security_headers_middleware.h"
#include "../utils/http_utils.h"
#include "../middlewares/authorization_middleware.h"
#include "http/handlers/relation_handlers/attach_many_to_many_handler.h"
#include "http/handlers/relation_handlers/detach_many_to_many_handler.h"
#include "http/handlers/relation_handlers/list_by_fk_field_handler.h"
#include "http/routing/paginated_match_rule.h"
#include "relation.h"
#include "service.h"
#include "runtime/generic_crud_engine.h"
#include "runtime/schema_runtime_registry.h"
#include "spdlog/spdlog.h"

#include <optional>

namespace sea::http::routing {

namespace {

using sea::application::HttpMethod;
using sea::application::RouteDefinition;

// ─────────────────────────────────────────────
// Helpers privés
// ─────────────────────────────────────────────

// CORRIGÉ : utilise la config de sécurité du service, pas is_auth_source
bool service_has_auth(const sea::domain::Service& service)
{
    return service.security.authentication().type()
    != sea::domain::security::AuthType::None;
}

bool is_crud_route(const RouteDefinition& route)
{
    return route.operation_name == "list"
           || route.operation_name == "create"
           || route.operation_name == "get_by_id"
           || route.operation_name == "update"
           || route.operation_name == "delete";
}

bool is_auth_route(const RouteDefinition& route)
{
    return route.entity_name == "Auth";
}

std::optional<seastar::httpd::operation_type>
to_seastar_operation(HttpMethod method)
{
    switch (method) {
    case HttpMethod::Get:
        return seastar::httpd::operation_type::GET;
    case HttpMethod::Post:
        return seastar::httpd::operation_type::POST;
    case HttpMethod::Put:
        return seastar::httpd::operation_type::PUT;
    case HttpMethod::Delete:
        return seastar::httpd::operation_type::DELETE;
    default:
        return std::nullopt;
    }
}

} // namespace

// ─────────────────────────────────────────────
// Composition des middlewares
// ─────────────────────────────────────────────

std::unique_ptr<seastar::httpd::handler_base> wrap_with_middlewares(
    std::unique_ptr<seastar::httpd::handler_base> handler,
    bool requires_auth,
    const MiddlewareContext& context)
{
    auto h = std::move(handler);

    // L'ordre du wrap est INVERSE de l'ordre d'exécution.
    // Plus une ligne est tardive, plus le middleware est extérieur,
    // donc plus il est exécuté tôt à l'arrivée d'une requête.

    // 6e exécuté : AuthorizationMiddleware (Module 5)
    // S'execute APRES ProtectedHandler (a besoin des X-User-* injectes)
    // Verifie les regles RBAC + ABAC subject-only
    if (requires_auth
        && context.policy_engine
        && context.service.access_control.enabled()) {
        h = sea::http::middlewares::apply_authorization(
            std::move(h),
            context.service.schema,
            context.service.access_control,
            context.policy_engine
            );
    }

    // 5e exécuté : Rate limit (peut lire X-User-Id injecté par Auth)
    if (context.rate_limit_store != nullptr
        && !context.service.security.rate_limits().empty()) {
        h = sea::http::middlewares::apply_rate_limit(
            std::move(h),
            context.service.security.rate_limits(),
            *context.rate_limit_store
            );
    }

    // 4e exécuté : Auth (injecte X-User-Id pour les middlewares en aval)
    if (requires_auth && context.auth_service) {
        h = sea::http::handlers::auth::maybe_protect(
            std::move(h),
            requires_auth,
            context.auth_service,
            context.token_tracking,
            context.blocking_executor,
            context.cookie_config
            );

    }

    // 3e exécuté : CORS (preflight, validation origin, headers)
    if (context.service.security.cors().is_enabled()) {
        h = sea::http::middlewares::apply_cors(
            std::move(h),
            context.service.security.cors()
            );
    }

    // 2e exécuté : Security headers (s'applique à toutes les réponses)
    h = sea::http::middlewares::apply_security_headers(
        std::move(h),
        context.service.security.security_headers()
        );

    // 1er exécuté : HTTP limits (rejette le plus tôt possible)
    h = sea::http::middlewares::apply_http_limits(
        std::move(h),
        context.service.security.http_limits()
        );

    return h;
}

// ─────────────────────────────────────────────
// Routes CRUD
// ─────────────────────────────────────────────

void register_collection_route(
    seastar::httpd::routes& routes,
    const RouteDefinition& route,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry>& registry,
    const MiddlewareContext& context)
{
    if (is_auth_route(route)) {
        return;
    }

    const auto operation = to_seastar_operation(route.method);
    if (!operation.has_value()) {
        return;
    }

    // CORRIGÉ : utilise service_has_auth, pas l'ancien entity_requires_auth
    const bool requires_auth = service_has_auth(context.service);

    if (route.operation_name == "list") {
        spdlog::get("sea.boot")->info(
            "[ROUTE] GET {} -> ListHandler {} ",
            route.path,
            requires_auth ? " 🔒" : "🌐"
            );

        auto handler = std::make_unique<sea::http::handlers::crud::ListHandler>(
            crud_engine,
            route.entity_name,
            context.resource_auth_helper
            );

        auto wrapped = wrap_with_middlewares(
            std::move(handler),
            requires_auth,
            context
            );

        routes.add(
            *operation,
            seastar::httpd::url(route.path),
            wrapped.release()
            );
        return;
    }

    if (route.operation_name == "create") {
        spdlog::get("sea.boot")->info(
            "[ROUTE] POST {} -> CreateHandler {} ",
            route.path,
            requires_auth ? " 🔒" : "🌐"
            );
        auto handler = std::make_unique<sea::http::handlers::crud::CreateHandler>(
            crud_engine,
            registry,
            route.entity_name,
            context.auth_service,
            context.service.database_config.type,
            context.blocking_executor,
            context.resource_auth_helper,
            context.file_extractor
            );

        auto wrapped = wrap_with_middlewares(
            std::move(handler),
            requires_auth,
            context
            );

        routes.add(
            *operation,
            seastar::httpd::url(route.path),
            wrapped.release()
            );
        return;
    }
}

void register_item_route(
    seastar::httpd::routes& routes,
    const RouteDefinition& route,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry>& registry,
    const MiddlewareContext& context)
{
    if (is_auth_route(route)) {
        return;
    }

    if (!is_crud_route(route)) {
        return;
    }

    const auto operation = to_seastar_operation(route.method);
    if (!operation.has_value()) {
        return;
    }

    const bool requires_auth = service_has_auth(context.service);
    const auto base_path = sea::http::utils::base_path_without_id_suffix(route.path);

    if (route.operation_name == "get_by_id") {
        spdlog::get("sea.boot")->info(
            "[ROUTE] GET {} -> GetByIdHandler {} ",
            route.path,
            requires_auth ? " 🔒" : "🌐"
            );
        auto handler = std::make_unique<sea::http::handlers::crud::GetByIdHandler>(
            crud_engine,
            route.entity_name,
            context.resource_auth_helper
            );

        auto wrapped = wrap_with_middlewares(
            std::move(handler),
            requires_auth,
            context
            );
        // Pattern strict /entity/{id} — sans le .remainder('id') qui
        // capturerait /entity/{id}/anything, empêchant les routes plus
        // spécifiques (ex: /entity/{id}/<file_field>) d'être atteintes.
        // build_match_rule_from_template fait du matching strict sur
        // le template, paramètre extrait via get_path_param('id').
        auto* rule = build_match_rule_from_template(
            route.path,
            wrapped.release()
            );
        routes.add(rule, *operation);
        return;
    }

    if (route.operation_name == "update") {
        spdlog::get("sea.boot")->info(
            "[ROUTE] PUT {} -> UpdateHandler {} ",
            route.path,
            requires_auth ? " 🔒" : "🌐"
            );
        auto handler = std::make_unique<sea::http::handlers::crud::UpdateHandler>(
            crud_engine,
            registry,
            context.auth_service,
            route.entity_name,
            context.blocking_executor,
            context.resource_auth_helper,
            context.file_extractor
            );

        auto wrapped = wrap_with_middlewares(
            std::move(handler),
            requires_auth,
            context
            );

        routes.add(
            *operation,
            seastar::httpd::url(base_path).remainder("id"),
            wrapped.release()
            );
        return;
    }

    if (route.operation_name == "delete") {
        spdlog::get("sea.boot")->info(
            "[ROUTE] DELETE {} -> DeleteHandler {} ",
            route.path,
            requires_auth ? " 🔒" : "🌐"
            );
        auto handler = std::make_unique<sea::http::handlers::crud::DeleteHandler>(
            crud_engine,
            route.entity_name,
            context.resource_auth_helper,
            registry,
            context.file_extractor
            );

        auto wrapped = wrap_with_middlewares(
            std::move(handler),
            requires_auth,
            context
            );

        routes.add(
            *operation,
            seastar::httpd::url(base_path).remainder("id"),
            wrapped.release()
            );
        return;
    }
}

// ─────────────────────────────────────────────
// Routes relationnelles
// ─────────────────────────────────────────────

void register_has_many_routes(
    seastar::httpd::routes& routes,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const MiddlewareContext& context)
{
    // CORRIGÉ : déterminé une seule fois pour le service entier
    const bool requires_auth = service_has_auth(context.service);

    for (const auto& entity : context.service.schema.entities) {
        for (const auto& relation : entity.relations) {
            if (relation.kind != sea::domain::RelationKind::HasMany) {
                continue;
            }

            const std::string base =
                "/" + sea::http::utils::lower_first(entity.name) + "s";

            // ───────────────────────────────────────────────────────────
            // Route 1 : GET /<parent>s/{id}/<children>
            //           → ListByFkHandler
            //           Ex: /departments/{id}/employees
            // ───────────────────────────────────────────────────────────
            {
                const std::string child_path =
                    base + "/{id}/" + relation.name;
                spdlog::get("sea.boot")->info(
                    "[ROUTE] GET {} -> ListByFkHandler {} ",
                    child_path,
                    requires_auth ? " 🔒" : "🌐"
                    );
                auto handler = std::make_unique<sea::http::handlers::relation::ListByFkHandler>(
                    crud_engine,
                    relation.target_entity,
                    relation.fk_column,
                    context.resource_auth_helper
                    );

                auto wrapped = wrap_with_middlewares(
                    std::move(handler),
                    requires_auth,
                    context
                    );

                routes.add(
                    seastar::httpd::operation_type::GET,
                    seastar::httpd::url(base).remainder("id"),
                    wrapped.release()
                    );
            }

            // ───────────────────────────────────────────────────────────
            // Route 2 : GET /<parent>s_with_<children>/{id}
            //             → GetWithChildrenHandler
            //             Ex: /departments_with_employees/{id}
            // ───────────────────────────────────────────────────────────
            {
                const std::string with_children_path =
                    "/" + sea::http::utils::lower_first(entity.name) + "s_with_" +
                    relation.name + "/{id}";
                spdlog::get("sea.boot")->info(
                    "[ROUTE] GET {} -> GetWithChildrenHandler {} ",
                    with_children_path,
                    requires_auth ? " 🔒" : "🌐"
                    );
                auto handler = std::make_unique<sea::http::handlers::relation::GetWithChildrenHandler>(
                    crud_engine,
                    entity.name,                      // parent_entity (Department)
                    relation.target_entity,           // child_entity (Employee)
                    relation.fk_column,               // fk_column (department_id)
                    relation.name,                    // children_key (employees)
                    context.resource_auth_helper
                    );

                auto wrapped = wrap_with_middlewares(
                    std::move(handler),
                    requires_auth,
                    context
                    );

                // URL builder : /<parent>s_with_<children>/{id}
                const std::string with_children_base =
                    "/" + sea::http::utils::lower_first(entity.name) + "s_with_" +
                    relation.name;

                routes.add(
                    seastar::httpd::operation_type::GET,
                    seastar::httpd::url(with_children_base).remainder("id"),
                    wrapped.release()
                    );
            }

            // ───────────────────────────────────────────────────────────
            // Route 3 : GET /<children>/filter/with_<parent>_name?name=<value>
            //             → ListByFkFieldHandler
            //             Ex: /employees/filter/with_department_name?name=IT
            //
            // SKIP si le parent n'a pas de champ "name"
            // ───────────────────────────────────────────────────────────
            {
                // Cherche le field "name" sur l'entite parent
                const sea::domain::Field* name_field = nullptr;
                for (const auto& field : entity.fields) {
                    if (field.name == "name") {
                        name_field = &field;
                        break;
                    }
                }

                if (name_field == nullptr) {
                    // Le parent n'a pas de champ "name", on skip cette route
                    spdlog::get("sea.boot")->debug(
                        "[ROUTE] SKIP filter/with_{} _name (no 'name' field on {})",
                        sea::http::utils::lower_first(entity.name),
                        entity.name
                        );
                } else {
                    // Construit le path : /<children>/filter/with_<parent>_name
                    // Note : 'children' = relation.target_entity en lower + "s"
                    const std::string filter_path =
                        "/" + sea::http::utils::lower_first(relation.target_entity) + "s" +
                        "/filter/with_" + sea::http::utils::lower_first(entity.name) + "_name";
                    spdlog::get("sea.boot")->info(
                        "[ROUTE] GET {} ?name=<value> -> ListByFkFieldHandler {}",
                        filter_path,
                        requires_auth ? " 🔒" : " 🌐"
                        );
                    auto handler = std::make_unique<sea::http::handlers::relation::ListByFkFieldHandler>(
                        crud_engine,
                        relation.target_entity,           // child_entity (Employee)
                        entity.name,                      // parent_entity (Department)
                        relation.fk_column,               // fk_column (department_id)
                        std::string("name"),              // search_field (convention)
                        context.resource_auth_helper
                        );

                    auto wrapped = wrap_with_middlewares(
                        std::move(handler),
                        requires_auth,
                        context
                        );

                    routes.add(
                        seastar::httpd::operation_type::GET,
                        seastar::httpd::url(filter_path).remainder("value"),
                        wrapped.release()
                        );
                }
            }
        }
    }
}

void register_has_one_routes(
    seastar::httpd::routes& routes,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const MiddlewareContext& context)
{
    const bool requires_auth = service_has_auth(context.service);

    for (const auto& entity : context.service.schema.entities) {
        for (const auto& relation : entity.relations) {
            if (relation.kind != sea::domain::RelationKind::HasOne) {
                continue;
            }

            const std::string base =
                "/" + sea::http::utils::lower_first(entity.name) + "s";

            const std::string path =
                base + "/{id}/" + relation.name;

            spdlog::get("sea.boot")->info(
                "[ROUTE] GET {} -> GetOneByFkHandler {}",
                path,
                requires_auth ? " 🔒" : " 🌐"
                );
            auto handler = std::make_unique<sea::http::handlers::relation::GetOneByFkHandler>(
                crud_engine,
                relation.target_entity,
                relation.fk_column,
                context.resource_auth_helper
                );

            auto wrapped = wrap_with_middlewares(
                std::move(handler),
                requires_auth,
                context
                );

            routes.add(
                seastar::httpd::operation_type::GET,
                seastar::httpd::url(base).remainder("id"),
                wrapped.release()
                );
        }
    }
}

void register_many_to_many_routes(
    seastar::httpd::routes& routes,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const MiddlewareContext& context)
{
    const bool requires_auth = service_has_auth(context.service);

    for (const auto& entity : context.service.schema.entities) {
        for (const auto& relation : entity.relations) {
            if (relation.kind != sea::domain::RelationKind::ManyToMany) {
                continue;
            }

            const std::string base =
                "/" + sea::http::utils::lower_first(entity.name) + "s";

            // ─────────────────────────────────────────────────────
            // GET /<entity>s/{id}/<relation>
            //
            // Liste les ressources cibles associees a la source.
            // (Comportement preserve, inchange par rapport a la version
            // precedente.)
            // ─────────────────────────────────────────────────────
            const std::string list_path =
                base + "/{id}/" + relation.name;

            spdlog::get("sea.boot")->info(
                "[ROUTE] GET {} -> ListManyToManyHandler {}",
                list_path,
                requires_auth ? " 🔒" : " 🌐"
                );

            auto list_handler = std::make_unique<sea::http::handlers::relation::ListManyToManyHandler>(
                crud_engine,
                relation.pivot_table,
                relation.target_entity,
                relation.source_fk_column,
                relation.target_fk_column,
                context.resource_auth_helper
                );

            auto list_wrapped = wrap_with_middlewares(
                std::move(list_handler),
                requires_auth,
                context
                );

            auto* list_rule = build_match_rule_from_template(
                list_path,
                list_wrapped.release()
                );
            routes.add(list_rule, seastar::httpd::operation_type::GET);
            // ─────────────────────────────────────────────────────
            // POST /<entity>s/{id}/<relation>/{target_id}
            //
            // Cree une association dans la table pivot.
            //
            // Cette route a DEUX path params, donc on doit utiliser
            // un match_rule (l'API .remainder() de Seastar n'accepte
            // qu'un seul path param).
            // ─────────────────────────────────────────────────────
            const std::string attach_path =
                base + "/{id}/" + relation.name + "/{target_id}";

            spdlog::get("sea.boot")->info(
                "[ROUTE] POST {} -> AttachManyToManyHandler {}",
                attach_path,
                requires_auth ? " 🔒" : " 🌐"
                );

            auto attach_handler = std::make_unique<sea::http::handlers::relation::AttachManyToManyHandler>(
                crud_engine,
                entity.name,               // source_entity
                relation.target_entity,
                relation.pivot_table,
                relation.source_fk_column,
                relation.target_fk_column,
                context.resource_auth_helper
                );

            auto attach_wrapped = wrap_with_middlewares(
                std::move(attach_handler),
                requires_auth,
                context
                );

            // build_match_rule_from_template parse le template
            // "{id}" et "{target_id}" et construit un match_rule
            // qui les exposera via req->get_path_param().
            auto* attach_rule = build_match_rule_from_template(
                attach_path,
                attach_wrapped.release()
                );

            routes.add(attach_rule, seastar::httpd::operation_type::POST);

            // ─────────────────────────────────────────────────────
            // DELETE /<entity>s/{id}/<relation>/{target_id}
            //
            // Supprime une association dans la table pivot.
            // ─────────────────────────────────────────────────────
            spdlog::get("sea.boot")->info(
                "[ROUTE] DELETE {} -> DetachManyToManyHandler {}",
                attach_path,
                requires_auth ? " 🔒" : " 🌐"
                );

            auto detach_handler = std::make_unique<sea::http::handlers::relation::DetachManyToManyHandler>(
                crud_engine,
                entity.name,               // source_entity
                relation.target_entity,
                relation.pivot_table,
                relation.source_fk_column,
                relation.target_fk_column,
                context.resource_auth_helper
                );

            auto detach_wrapped = wrap_with_middlewares(
                std::move(detach_handler),
                requires_auth,
                context
                );

            auto* detach_rule = build_match_rule_from_template(
                attach_path,        // meme template que POST
                detach_wrapped.release()
                );

            routes.add(detach_rule, seastar::httpd::operation_type::DELETE);
        }
    }
}


// ─────────────────────────────────────────────
// Routes de download de fichiers
//
// Pour chaque champ File de chaque entité, génère une route :
//   GET /<entity_lower>s/{id}/<field>
//
// Skip si :
//   - context.file_service est null (le service backend n'a pas de
//     support fichiers configuré, ou le YAML n'utilise pas de champ File)
//   - l'entité n'a aucun champ File (rien à exposer)
//
// L'ABAC est héritée de l'entité parente (CrudOperation::Read).
// ─────────────────────────────────────────────
void register_file_download_routes(
    seastar::httpd::routes& routes,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry>& registry,
    const MiddlewareContext& context)
{
    if (context.file_service == nullptr) {
        // Pas de support fichiers : ne crée aucune route download.
        return;
    }

    const bool requires_auth = service_has_auth(context.service);

    for (const auto& entity : context.service.schema.entities) {
        // Base path : /users, /documents, etc. (lower + 's' selon convention).
        const std::string base =
            "/" + sea::http::utils::lower_first(entity.name) + "s";

        for (const auto& field : entity.fields) {
            if (!field.is_file_field()) {
                continue;
            }

            // Route : /<entity>s/{id}/<field>
            // Le seastar router fait correspondre la dernière partie
            // statique avec .remainder("id"). Comme on a /users/{id}/avatar,
            // on ne peut PAS utiliser .remainder("id") directement parce
            // que le path ne se termine pas par {id}.
            //
            // Il faut faire /users/{id}/avatar - Seastar gère bien les
            // path params multiples via url(...).remainder("id") quand
            // {id} est dernier, mais ici {id} est au milieu suivi du
            // segment statique "avatar".
            //
            // Workaround : on construit l'URL avec le segment intermédiaire
            // statique avant le remainder.
            //
            // Méthode utilisée : on enregistre l'URL via path direct
            // "/users/{id}/avatar" et on extrait l'id via get_path_param.
            const std::string full_path =
                base + "/{id}/" + field.name;

            spdlog::get("sea.boot")->info(
                "[ROUTE] GET {} -> FileDownloadByFieldHandler {} ",
                full_path,
                requires_auth ? " 🔒" : " 🌐"
                );

            auto handler =
                std::make_unique<sea::http::handlers::files::FileDownloadByFieldHandler>(
                    crud_engine,
                    registry,
                    context.file_service,
                    entity.name,
                    field.name,
                    context.resource_auth_helper
                    );

            auto wrapped = wrap_with_middlewares(
                std::move(handler),
                requires_auth,
                context
                );

            // Pattern d'URL : /documents/{id}/attachment
            //
            // On utilise build_match_rule_from_template (cf.
            // paginated_match_rule.h) plutôt que url(...).remainder().
            //
            // Pourquoi : url("/documents/attachment").remainder("id")
            // enregistre en réalité la route /documents/attachment/{id},
            // pas /documents/{id}/attachment — l'ordre du segment statique
            // et du remainder est inversé. Et url("/documents").remainder("id")
            // matche /documents/<n'importe quoi>, donc avale l'URL bien
            // avant que /documents/{id}/attachment ne soit testée.
            //
            // match_rule, lui, gère correctement un paramètre AU MILIEU
            // de l'URL — c'est pour ça que la pagination l'utilise pour
            // /entity/{id}/page, etc.
            auto* rule = build_match_rule_from_template(
                full_path,
                wrapped.release()
                );
            routes.add(rule, seastar::httpd::operation_type::GET);
        }
    }
}

void log_route_definitions(
    const std::string& service_name,
    const std::vector<RouteDefinition>& route_definitions)
{
    auto log = spdlog::get("sea.boot");
    log->info("[ROUTES] Service {}", service_name);
    for (const auto& route : route_definitions) {
        log->info("  {} -> {}.{}", route.path, route.entity_name, route.operation_name);
    }

}

} // namespace sea::http::routing