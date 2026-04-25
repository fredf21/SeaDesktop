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
#include "../utils/http_utils.h"

#include "authservice.h"
#include "relation.h"
#include "service.h"
#include "runtime/generic_crud_engine.h"
#include "runtime/schema_runtime_registry.h"
#include "service.h"

#include <iostream>
#include <optional>

namespace sea::http::routing {

namespace {

using sea::application::HttpMethod;
using sea::application::RouteDefinition;

bool entity_requires_auth(
    const sea::domain::Service& service,
    const std::string& entity_name)
{
    for (const auto& entity : service.schema.entities) {
        if (entity.name == entity_name) {
            return entity.options.enable_auth;
        }
    }

    return false;
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

void register_collection_route(
    seastar::httpd::routes& routes,
    const RouteDefinition& route,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry>& registry,
    const sea::domain::Service& service,
    const std::shared_ptr<sea::application::AuthService>& auth_service)
{
    if (is_auth_route(route)) {
        return;
    }

    const auto operation = to_seastar_operation(route.method);
    if (!operation.has_value()) {
        return;
    }

    const bool requires_auth = entity_requires_auth(service, route.entity_name);

    if (route.operation_name == "list") {
        std::cerr << "[ROUTE] GET " << route.path << " -> ListHandler\n";

        routes.add(
            *operation,
            seastar::httpd::url(route.path),
            sea::http::handlers::auth::maybe_protect_handler(
                std::make_unique<sea::http::handlers::crud::ListHandler>(
                    crud_engine,
                    route.entity_name),
                requires_auth,
                auth_service));

        return;
    }

    if (route.operation_name == "create") {
        std::cerr << "[ROUTE] POST " << route.path << " -> CreateHandler\n";

        routes.add(
            *operation,
            seastar::httpd::url(route.path),
            sea::http::handlers::auth::maybe_protect_handler(
                std::make_unique<sea::http::handlers::crud::CreateHandler>(
                    crud_engine,
                    registry,
                    route.entity_name,
                    auth_service,
                    service.database_config.type),
                requires_auth,
                auth_service));

        return;
    }
}

void register_item_route(
    seastar::httpd::routes& routes,
    const RouteDefinition& route,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry>& registry,
    const sea::domain::Service& service,
    const std::shared_ptr<sea::application::AuthService>& auth_service)
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

    const bool requires_auth = entity_requires_auth(service, route.entity_name);
    const auto base_path = sea::http::utils::base_path_without_id_suffix(route.path);

    if (route.operation_name == "get_by_id") {
        std::cerr << "[ROUTE] GET " << route.path << " -> GetByIdHandler\n";

        routes.add(
            *operation,
            seastar::httpd::url(base_path).remainder("id"),
            sea::http::handlers::auth::maybe_protect_handler(
                std::make_unique<sea::http::handlers::crud::GetByIdHandler>(
                    crud_engine,
                    route.entity_name),
                requires_auth,
                auth_service));

        return;
    }

    if (route.operation_name == "update") {
        std::cerr << "[ROUTE] PUT " << route.path << " -> UpdateHandler\n";

        routes.add(
            *operation,
            seastar::httpd::url(base_path).remainder("id"),
            sea::http::handlers::auth::maybe_protect_handler(
                std::make_unique<sea::http::handlers::crud::UpdateHandler>(
                    crud_engine,
                    registry,
                    auth_service,
                    route.entity_name),
                requires_auth,
                auth_service));

        return;
    }

    if (route.operation_name == "delete") {
        std::cerr << "[ROUTE] DELETE " << route.path << " -> DeleteHandler\n";

        routes.add(
            *operation,
            seastar::httpd::url(base_path).remainder("id"),
            sea::http::handlers::auth::maybe_protect_handler(
                std::make_unique<sea::http::handlers::crud::DeleteHandler>(
                    crud_engine,
                    route.entity_name),
                requires_auth,
                auth_service));

        return;
    }
}

void register_has_many_routes(
    seastar::httpd::routes& routes,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const sea::domain::Service& service,
    const std::shared_ptr<sea::application::AuthService>& auth_service)
{
    for (const auto& entity : service.schema.entities) {
        const bool requires_auth = entity.options.enable_auth;

        for (const auto& relation : entity.relations) {
            if (relation.kind != sea::domain::RelationKind::HasMany) {
                continue;
            }

            const std::string base =
                "/" + sea::http::utils::lower_first(entity.name) + "s";

            const std::string child_path =
                base + "/{id}/" + relation.name;

            std::cerr << "[ROUTE] GET " << child_path << " -> ListByFkHandler\n";

            routes.add(
                seastar::httpd::operation_type::GET,
                seastar::httpd::url(base).remainder("id"),
                sea::http::handlers::auth::maybe_protect_handler(
                    std::make_unique<sea::http::handlers::relation::ListByFkHandler>(
                        crud_engine,
                        relation.target_entity,
                        relation.fk_column),
                    requires_auth,
                    auth_service));
        }
    }
}

void register_has_one_routes(
    seastar::httpd::routes& routes,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const sea::domain::Service& service,
    const std::shared_ptr<sea::application::AuthService>& auth_service)
{
    for (const auto& entity : service.schema.entities) {
        const bool requires_auth = entity.options.enable_auth;

        for (const auto& relation : entity.relations) {
            if (relation.kind != sea::domain::RelationKind::HasOne) {
                continue;
            }

            const std::string base =
                "/" + sea::http::utils::lower_first(entity.name) + "s";

            const std::string path =
                base + "/{id}/" + relation.name;

            std::cerr << "[ROUTE] GET " << path << " -> GetOneByFkHandler\n";

            routes.add(
                seastar::httpd::operation_type::GET,
                seastar::httpd::url(base).remainder("id"),
                sea::http::handlers::auth::maybe_protect_handler(
                    std::make_unique<sea::http::handlers::relation::GetOneByFkHandler>(
                        crud_engine,
                        relation.target_entity,
                        relation.fk_column),
                    requires_auth,
                    auth_service));
        }
    }
}

void register_many_to_many_routes(
    seastar::httpd::routes& routes,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const sea::domain::Service& service,
    const std::shared_ptr<sea::application::AuthService>& auth_service)
{
    for (const auto& entity : service.schema.entities) {
        const bool requires_auth = entity.options.enable_auth;

        for (const auto& relation : entity.relations) {
            if (relation.kind != sea::domain::RelationKind::ManyToMany) {
                continue;
            }

            const std::string base =
                "/" + sea::http::utils::lower_first(entity.name) + "s";

            const std::string path =
                base + "/{id}/" + relation.name;

            std::cerr << "[ROUTE] GET " << path << " -> ListManyToManyHandler\n";

            routes.add(
                seastar::httpd::operation_type::GET,
                seastar::httpd::url(base).remainder("id"),
                sea::http::handlers::auth::maybe_protect_handler(
                    std::make_unique<sea::http::handlers::relation::ListManyToManyHandler>(
                        crud_engine,
                        relation.pivot_table,
                        relation.target_entity,
                        relation.source_fk_column,
                        relation.target_fk_column),
                    requires_auth,
                    auth_service));
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
