#include "route_generator.h"

namespace sea::application {

std::vector<RouteDefinition>
RouteGenerator::generate(const sea::domain::Schema& schema) const {
    std::vector<RouteDefinition> routes;

    for (const auto& entity : schema.entities) {
        auto entity_routes = generate_for_entity(entity);
        routes.insert(routes.end(), entity_routes.begin(), entity_routes.end());
    }

    return routes;
}

std::vector<RouteDefinition>
RouteGenerator::generate_for_entity(const sea::domain::Entity& entity) const {
    std::vector<RouteDefinition> routes;

    if (!entity.options.enable_crud) {
        return routes;
    }

    const std::string base_path = entity.route_prefix();

    // CRUD standard
    routes.push_back({
        .method = HttpMethod::Get,
        .path = base_path,
        .entity_name = entity.name,
        .operation_name = "list"
    });

    routes.push_back({
        .method = HttpMethod::Get,
        .path = base_path + "/{id}",
        .entity_name = entity.name,
        .operation_name = "get_by_id"
    });

    routes.push_back({
        .method = HttpMethod::Post,
        .path = base_path,
        .entity_name = entity.name,
        .operation_name = "create"
    });

    routes.push_back({
        .method = HttpMethod::Put,
        .path = base_path + "/{id}",
        .entity_name = entity.name,
        .operation_name = "update"
    });

    routes.push_back({
        .method = HttpMethod::Delete,
        .path = base_path + "/{id}",
        .entity_name = entity.name,
        .operation_name = "delete"
    });

    // Auth logique si activée
    if (entity.options.enable_auth) {
        routes.push_back({
            .method = HttpMethod::Post,
            .path = base_path + "/register",
            .entity_name = entity.name,
            .operation_name = "register"
        });

        routes.push_back({
            .method = HttpMethod::Post,
            .path = base_path + "/login",
            .entity_name = entity.name,
            .operation_name = "login"
        });

        routes.push_back({
            .method = HttpMethod::Post,
            .path = base_path + "/token",
            .entity_name = entity.name,
            .operation_name = "token"
        });
    }

    return routes;
}

} // namespace sea::application
