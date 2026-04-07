#ifndef OPENAPIGENERATOR_H
#define OPENAPIGENERATOR_H
#include "service.h"
#include <nlohmann/json.hpp>
#include "route_generator.h"

namespace sea::application {
class OpenApiGenerator
{
public:
    OpenApiGenerator();
    [[nodiscard]] nlohmann::json generate(
        const sea::domain::Service& service,
        const std::vector<RouteDefinition>& route_definitions
        ) const;
bool entity_requires_auth(
    const domain::Service& service,
        const std::string& entity_name) const;
private:
    using json = nlohmann::json;
    [[nodiscard]] json make_entity_schema(const sea::domain::Entity& entity) const;
    [[nodiscard]] json make_entity_input_schema(const sea::domain::Entity& entity) const;
    [[nodiscard]] json field_to_openapi_schema(const sea::domain::Field& field) const;
    void add_crud_path(
        json& paths,
        const RouteDefinition& route,
        const sea::domain::Service& service) const;
    void add_relation_paths(json& paths, const sea::domain::Service& service) const;
    [[nodiscard]] std::string to_openapi_method(sea::application::HttpMethod method) const;
};
}
#endif // OPENAPIGENERATOR_H
