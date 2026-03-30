#pragma once

#include <string>
#include <vector>

#include "schema.h"
#include "entity.h"

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
    generate(const sea::domain::Schema& schema) const;

private:
    [[nodiscard]] std::vector<RouteDefinition>
    generate_for_entity(const sea::domain::Entity& entity) const;
};

} // namespace sea::application