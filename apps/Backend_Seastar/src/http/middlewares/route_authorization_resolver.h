#ifndef SEA_HTTP_MIDDLEWARES_ROUTE_AUTHORIZATION_RESOLVER_H
#define SEA_HTTP_MIDDLEWARES_ROUTE_AUTHORIZATION_RESOLVER_H

#include "access_control/crud_operation.h"
#include "schema.h"

#include <optional>
#include <string>
#include <vector>

namespace sea::http::middlewares {

/**
 * Une operation a verifier sur une entite.
 *
 * Exemple :
 *   AuthorizationCheck{
 *     entity_name = "Department",
 *     operation   = CrudOperation::GetById,
 *     description = "parent department check"
 *   }
 */
struct AuthorizationCheck {
    std::string entity_name;
    sea::domain::access_control::CrudOperation operation;
    std::string description;     // Pour les logs et messages d'erreur
};

/**
 * Plan d'autorisation pour une route donnee.
 *
 * Strategie C (double check) : pour les routes relationnelles, on verifie
 * a la fois l'entite parent ET l'entite retournee.
 *
 * Exemple pour GET /employees/filter/with_department/{id} :
 *   checks = [
 *     {Department, GetById, "parent department"},
 *     {Employee,   List,    "child employees"}
 *   ]
 */
struct RouteAuthorizationPlan {
    std::vector<AuthorizationCheck> checks;
    bool unknown_route = false;   // true si la route n'a pas pu etre identifiee
};

/**
 * Resolveur de routes : mappe (method, path) vers une liste de checks
 * d'autorisation a effectuer.
 *
 * Routes supportees :
 *   - CRUD standard            : /<entity>s, /<entity>s/{id}
 *   - Filter by parent FK      : /<entity>s/filter/with_<parent>/{id}
 *   - Filter by parent field   : /<entity>s/filter/with_<parent>_<field>/{value}
 *   - Item relation            : /<entity>s/{id}/<relation>
 *   - Parent with children     : /<parent>s_with_<children>/{id}
 *   - Sub-collection           : /<entity>s/{id}/<relation>
 *
 * Pour les routes inconnues, retourne unknown_route=true (le middleware
 * decidera fail-closed).
 */
class RouteAuthorizationResolver {
public:
    explicit RouteAuthorizationResolver(const sea::domain::Schema& schema);

    [[nodiscard]] RouteAuthorizationPlan resolve(
        const std::string& method,
        const std::string& path
        ) const;

private:
    const sea::domain::Schema& schema_;

    // Pour chaque pattern, retourne un plan ou nullopt si pas de match
    std::optional<RouteAuthorizationPlan> try_match_crud(
        const std::string& method,
        const std::string& path
        ) const;

    std::optional<RouteAuthorizationPlan> try_match_filter_by_parent(
        const std::string& method,
        const std::string& path
        ) const;

    std::optional<RouteAuthorizationPlan> try_match_filter_by_parent_field(
        const std::string& method,
        const std::string& path
        ) const;

    std::optional<RouteAuthorizationPlan> try_match_item_relation(
        const std::string& method,
        const std::string& path
        ) const;

    std::optional<RouteAuthorizationPlan> try_match_parent_with_children(
        const std::string& method,
        const std::string& path
        ) const;
};

} // namespace sea::http::middlewares

#endif // SEA_HTTP_MIDDLEWARES_ROUTE_AUTHORIZATION_RESOLVER_H