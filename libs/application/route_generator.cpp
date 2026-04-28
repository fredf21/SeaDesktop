#include "route_generator.h"
#include "service.h"

#include <cctype>

namespace sea::application {

namespace {

std::string lower_first(std::string value) {
    if (!value.empty()) {
        value[0] = static_cast<char>(
            std::tolower(static_cast<unsigned char>(value[0]))
            );
    }
    return value;
}

std::string plural_path_from_entity(const std::string& entity_name) {
    return "/" + lower_first(entity_name) + "s";
}

bool schema_has_auth_source(const sea::domain::Schema& schema) {
    return std::ranges::any_of(schema.entities,
                               [](const sea::domain::Entity& e) {
                                   return e.options.is_auth_source;
                               }
                               );
}

} // namespace

std::vector<RouteDefinition>
RouteGenerator::generate(const sea::domain::Service& service) const {
    std::vector<RouteDefinition> routes;

    // Détermine une fois pour toutes si l'auth est activée au niveau service
    const bool needs_auth = service_requires_auth(service);

    const auto& schema = service.schema;

    // CRUD
    for (const auto& entity : schema.entities) {
        auto entity_routes = generate_for_entity(entity, needs_auth);
        routes.insert(routes.end(), entity_routes.begin(), entity_routes.end());
    }

    // Auth globale réelle
    if (schema_has_auth_source(schema)) {
        // Routes publiques
        routes.push_back({
            .method = HttpMethod::Post,
            .path = "/auth/register",
            .entity_name = "Auth",
            .operation_name = "register",
            .requires_auth = false                  // public
        });

        routes.push_back({
            .method = HttpMethod::Post,
            .path = "/auth/login",
            .entity_name = "Auth",
            .operation_name = "login",
            .requires_auth = false                  // public
        });

        // Route protégée
        routes.push_back({
            .method = HttpMethod::Get,
            .path = "/auth/me",
            .entity_name = "Auth",
            .operation_name = "me",
            .requires_auth = true                   // protégée (besoin du JWT)
        });

        // ajouts utiles si auth activée
        routes.push_back({
            .method = HttpMethod::Post,
            .path = "/auth/refresh",
            .entity_name = "Auth",
            .operation_name = "refresh",
            .requires_auth = false                  // public (utilise refresh_token)
        });

        routes.push_back({
            .method = HttpMethod::Post,
            .path = "/auth/logout",
            .entity_name = "Auth",
            .operation_name = "logout",
            .requires_auth = true                   // protégée
        });
    }

    // Relations
    for (const auto& entity : schema.entities) {
        const std::string parent_path = plural_path_from_entity(entity.name);
        const std::string parent_name = lower_first(entity.name);

        for (const auto& relation : entity.relations) {
            if (relation.kind == sea::domain::RelationKind::HasMany) {
                const std::string child_path = plural_path_from_entity(relation.target_entity);

                // /children/filter/with_parent/{id}
                routes.push_back({
                    .method = HttpMethod::Get,
                    .path = child_path + "/filter/with_" + parent_name + "/{id}",
                    .entity_name = relation.target_entity,
                    .operation_name = "list_by_fk",
                    .requires_auth = needs_auth         // ← AJOUT
                });

                // /children/filter/with_parent_<unique_field>/{value}
                for (const auto& field : entity.fields) {
                    if (!field.unique || field.name == "id") {
                        continue;
                    }

                    routes.push_back({
                        .method = HttpMethod::Get,
                        .path = child_path + "/filter/with_" + parent_name + "_" + field.name + "/{value}",
                        .entity_name = relation.target_entity,
                        .operation_name = "list_by_fk_field",
                        .requires_auth = needs_auth     // ← AJOUT
                    });
                }

                // /parents_with_relation/{id}
                routes.push_back({
                    .method = HttpMethod::Get,
                    .path = parent_path + "_with_" + relation.name + "/{id}",
                    .entity_name = entity.name,
                    .operation_name = "get_with_children",
                    .requires_auth = needs_auth         // ← AJOUT
                });
            }

            if (relation.kind == sea::domain::RelationKind::HasOne) {
                routes.push_back({
                    .method = HttpMethod::Get,
                    .path = parent_path + "/" + relation.name + "/{id}",
                    .entity_name = relation.target_entity,
                    .operation_name = "get_one_by_fk",
                    .requires_auth = needs_auth         // ← AJOUT
                });
            }

            if (relation.kind == sea::domain::RelationKind::ManyToMany) {
                const std::string target_path = plural_path_from_entity(relation.target_entity);

                routes.push_back({
                    .method = HttpMethod::Get,
                    .path = target_path + "/filter/with_" + parent_name + "/{id}",
                    .entity_name = relation.target_entity,
                    .operation_name = "list_many_to_many",
                    .requires_auth = needs_auth         // ← AJOUT
                });
            }
        }
    }

    return routes;
}

std::vector<RouteDefinition>
RouteGenerator::generate_for_entity(const sea::domain::Entity& entity, bool requires_auth) const {
    std::vector<RouteDefinition> routes;

    if (!entity.options.enable_crud) {
        return routes;
    }

    const std::string base_path = entity.route_prefix();

    routes.push_back({
        .method = HttpMethod::Get,
        .path = base_path,
        .entity_name = entity.name,
        .operation_name = "list",
        .requires_auth = requires_auth
    });

    routes.push_back({
        .method = HttpMethod::Get,
        .path = base_path + "/{id}",
        .entity_name = entity.name,
        .operation_name = "get_by_id",
        .requires_auth = requires_auth
    });

    routes.push_back({
        .method = HttpMethod::Post,
        .path = base_path,
        .entity_name = entity.name,
        .operation_name = "create",
        .requires_auth = requires_auth
    });

    routes.push_back({
        .method = HttpMethod::Put,
        .path = base_path + "/{id}",
        .entity_name = entity.name,
        .operation_name = "update",
        .requires_auth = requires_auth
    });

    routes.push_back({
        .method = HttpMethod::Delete,
        .path = base_path + "/{id}",
        .entity_name = entity.name,
        .operation_name = "delete",
        .requires_auth = requires_auth
    });

    return routes;
}
bool RouteGenerator::service_requires_auth(
    const sea::domain::Service& service) const
{
    // Si l'auth est désactivée au niveau service → toutes les routes CRUD publiques
    // Si l'auth est activée → toutes les routes CRUD protégées

    return service.security.authentication().type()
           != sea::domain::security::AuthType::None;
}
} // namespace sea::application