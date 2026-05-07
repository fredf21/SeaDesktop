#ifndef SEA_DOMAIN_ACCESS_CONTROL_POLICY_VALUE_SOURCE_H
#define SEA_DOMAIN_ACCESS_CONTROL_POLICY_VALUE_SOURCE_H

#include <optional>
#include <string>
#include <string_view>

namespace sea::domain::access_control {

/**
 * Source d'une valeur dans une condition.
 *
 * Literal  : une valeur statique inscrite dans le YAML
 *            ex : { literal: "admin" }
 *
 * Subject  : un attribut du user qui fait la requête (issu du JWT)
 *            ex : { source: subject, path: "roles" }
 *                 { source: subject, path: "attributes.department_id" }
 *
 * Resource : un attribut de la ressource visée (chargée depuis la DB)
 *            ex : { source: resource, path: "attributes.owner_id" }
 *
 * Context  : un attribut du contexte de la requête (heure, IP, méthode HTTP)
 *            ex : { source: context, path: "time.hour" }
 *                 { source: context, path: "request.ip" }
 */
enum class PolicyValueSource {
    Literal,
    Subject,
    Resource,
    Context
};

std::string_view to_string(PolicyValueSource s) noexcept;

std::optional<PolicyValueSource> policy_value_source_from_string(
    const std::string& s) noexcept;

} // namespace sea::domain::access_control

#endif // SEA_DOMAIN_ACCESS_CONTROL_POLICY_VALUE_SOURCE_H