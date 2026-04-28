#pragma once

#include <string>
#include <vector>

#include "entity.h"
#include "service.h"

namespace sea::application {

// ─────────────────────────────────────────────────────────────
// Méthode HTTP logique
// ─────────────────────────────────────────────────────────────
enum class HttpMethod {
    Get,
    Post,
    Put,
    Delete
};

// ─────────────────────────────────────────────────────────────
// Définition logique d’une route
//
// Sert au MVP pour brancher le runtime générique.
// Servira aussi plus tard au code generator.
// ─────────────────────────────────────────────────────────────
struct RouteDefinition {
    HttpMethod method;
    std::string path;
    std::string entity_name;
    std::string operation_name;
    bool requires_auth = false;
};

// ─────────────────────────────────────────────────────────────
// RouteGenerator
//
// Produit les routes CRUD logiques à partir d’un Schema.
// Respecte les EntityOptions (enable_crud, enable_auth, ...).
// ─────────────────────────────────────────────────────────────
class RouteGenerator {
public:
    [[nodiscard]] std::vector<RouteDefinition>
    generate(const sea::domain::Service& service) const;

private:
    [[nodiscard]] std::vector<RouteDefinition>
    generate_for_entity(const sea::domain::Entity& entity, bool requires_auth) const;
    bool service_requires_auth(const sea::domain::Service& service) const;
};

} // namespace sea::application