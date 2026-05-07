#ifndef SEA_DOMAIN_ACCESS_CONTROL_POLICY_OPERATOR_H
#define SEA_DOMAIN_ACCESS_CONTROL_POLICY_OPERATOR_H

#include <optional>
#include <string>
#include <string_view>

namespace sea::domain::access_control {

/**
 * Opérateurs de comparaison supportés par le moteur ABAC.
 *
 * Equals / NotEquals : égalité stricte de valeurs (string ou number)
 * In / NotIn : appartenance à une liste (left dans right)
 * Contains : vérifie qu'une liste/string contient un élément
 * StartsWith / EndsWith : pour les strings
 * GreaterThan / LessThan / >= / <= : comparaison numérique
 * Exists / NotExists : présence d'une valeur (right ignoré)
 * RegexMatch : correspondance regex (right doit être un literal regex)
 * Intersects : deux listes ont au moins un élément commun
 */
enum class PolicyOperator {
    Equals,
    NotEquals,
    In,
    NotIn,
    Contains,
    StartsWith,
    EndsWith,
    GreaterThan,
    GreaterThanOrEqual,
    LessThan,
    LessThanOrEqual,
    Exists,
    NotExists,
    RegexMatch,
    Intersects        // utile pour roles: subject.roles intersects [admin, manager]
};

/**
 * Convertit un PolicyOperator en string pour serialization (YAML, logs, OpenAPI).
 */
std::string_view to_string(PolicyOperator op) noexcept;

/**
 * Parse un string en PolicyOperator. Retourne nullopt si inconnu.
 * Accepte les variantes : "equals", "Equals", "==", "eq", etc.
 */
std::optional<PolicyOperator> policy_operator_from_string(
    const std::string& s) noexcept;

} // namespace sea::domain::access_control

#endif // SEA_DOMAIN_ACCESS_CONTROL_POLICY_OPERATOR_H