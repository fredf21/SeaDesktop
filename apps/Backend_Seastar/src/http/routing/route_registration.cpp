#include "route_registration.h"

#include "../handlers/auth_handlers/protected_handler.h"
#include "../handlers/crud_handlers/create_handler.h"
#include "../handlers/crud_handlers/delete_handler.h"
#include "../handlers/crud_handlers/get_by_id_handler.h"
#include "../handlers/crud_handlers/list_handler.h"
#include "../handlers/crud_handlers/update_handler.h"
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
#include "http/handlers/relation_handlers/list_by_fk_field_handler.h"
#include "relation.h"
#include "service.h"
#include "runtime/generic_crud_engine.h"
#include "runtime/schema_runtime_registry.h"

#include <iostream>
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
            context.blocking_executor
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
        if (seastar::this_shard_id() == 0) {
            std::cerr << "[ROUTE] GET " << route.path << " -> ListHandler"
                      << (requires_auth ? " 🔒" : " 🌐") << "\n";
        }

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
        if (seastar::this_shard_id() == 0) {
            std::cerr << "[ROUTE] POST " << route.path << " -> CreateHandler"
                      << (requires_auth ? " 🔒" : " 🌐") << "\n";
        }

        auto handler = std::make_unique<sea::http::handlers::crud::CreateHandler>(
            crud_engine,
            registry,
            route.entity_name,
            context.auth_service,
            context.service.database_config.type,
            context.blocking_executor,
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
        if (seastar::this_shard_id() == 0) {
            std::cerr << "[ROUTE] GET " << route.path << " -> GetByIdHandler"
                      << (requires_auth ? " 🔒" : " 🌐") << "\n";
        }

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

        routes.add(
            *operation,
            seastar::httpd::url(base_path).remainder("id"),
            wrapped.release()
            );
        return;
    }

    if (route.operation_name == "update") {
        if (seastar::this_shard_id() == 0) {
            std::cerr << "[ROUTE] PUT " << route.path << " -> UpdateHandler"
                      << (requires_auth ? " 🔒" : " 🌐") << "\n";
        }

        auto handler = std::make_unique<sea::http::handlers::crud::UpdateHandler>(
            crud_engine,
            registry,
            context.auth_service,
            route.entity_name,
            context.blocking_executor,
            context.resource_auth_helper
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
        if (seastar::this_shard_id() == 0) {
            std::cerr << "[ROUTE] DELETE " << route.path << " -> DeleteHandler"
                      << (requires_auth ? " 🔒" : " 🌐") << "\n";
        }


        auto handler = std::make_unique<sea::http::handlers::crud::DeleteHandler>(
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

                if (seastar::this_shard_id() == 0) {
                    std::cerr << "[ROUTE] GET " << child_path << " -> ListByFkHandler"
                              << (requires_auth ? " 🔒" : " 🌐") << "\n";
                }

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

                if (seastar::this_shard_id() == 0) {
                    std::cerr << "[ROUTE] GET " << with_children_path << " -> GetWithChildrenHandler"
                              << (requires_auth ? " 🔒" : " 🌐") << "\n";
                }

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
                    if (seastar::this_shard_id() == 0) {
                        std::cerr << "[ROUTE] SKIP filter/with_"
                                  << sea::http::utils::lower_first(entity.name)
                                  << "_name (no 'name' field on " << entity.name << ")\n";
                    }
                } else {
                    // Construit le path : /<children>/filter/with_<parent>_name
                    // Note : 'children' = relation.target_entity en lower + "s"
                    const std::string filter_path =
                        "/" + sea::http::utils::lower_first(relation.target_entity) + "s" +
                        "/filter/with_" + sea::http::utils::lower_first(entity.name) + "_name";

                    if (seastar::this_shard_id() == 0) {
                        std::cerr << "[ROUTE] GET " << filter_path
                                  << "?name=<value> -> ListByFkFieldHandler"
                                  << (requires_auth ? " 🔒" : " 🌐") << "\n";
                    }

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
            if (seastar::this_shard_id() == 0) {
                std::cerr << "[ROUTE] GET " << path << " -> GetOneByFkHandler"
                          << (requires_auth ? " 🔒" : " 🌐") << "\n";
            }


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

            const std::string path =
                base + "/{id}/" + relation.name;

            if (seastar::this_shard_id() == 0) {
                std::cerr << "[ROUTE] GET " << path << " -> ListManyToManyHandler"
                          << (requires_auth ? " 🔒" : " 🌐") << "\n";
            }


            auto handler = std::make_unique<sea::http::handlers::relation::ListManyToManyHandler>(
                crud_engine,
                relation.pivot_table,
                relation.target_entity,
                relation.source_fk_column,
                relation.target_fk_column,
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

void log_route_definitions(
    const std::string& service_name,
    const std::vector<RouteDefinition>& route_definitions)
{
    std::cerr << "[ROUTES] Service " << service_name << "\n";

    for (const auto& route : route_definitions) {
        std::cerr << "  "
                  << route.path
                  << " -> "
                  << route.entity_name
                  << "."
                  << route.operation_name
                  << "\n";
    }
}

} // namespace sea::http::routing