#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "route_generator.h"
#include "schema.h"
#include "service.h"

namespace sea::application {

class OpenApiGenerator {
public:
    using json = nlohmann::json;

    OpenApiGenerator();

    json generate(
        const domain::Service& service,
        const std::vector<RouteDefinition>& route_definitions
        ) const;

private:
    // Helpers de sécurité
    bool service_has_auth(const domain::Service& service) const;
    bool schema_has_auth_source(const domain::Schema& schema) const;

    // Génération de schémas
    json make_entity_schema(const domain::Entity& entity) const;
    json make_entity_input_schema(const domain::Entity& entity) const;
    json field_to_openapi_schema(const domain::Field& field) const;
    void add_auth_schemas(json& schemas) const;

    // Génération de paths
    void add_crud_path(
        json& paths,
        const RouteDefinition& route,
        const domain::Service& service
        ) const;

    void add_relation_paths(
        json& paths,
        const domain::Service& service
        ) const;

    void add_auth_paths(json& paths) const;
    void add_health_path(json& paths) const;

    // Conversion HTTP method
    std::string to_openapi_method(HttpMethod method) const;

    // Helper pour construire la security clause
    json bearer_security() const;
};

} // namespace sea::application