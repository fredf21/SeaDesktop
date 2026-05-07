#ifndef SEA_DOMAIN_ACCESS_CONTROL_POLICY_CONDITION_TYPE_H
#define SEA_DOMAIN_ACCESS_CONTROL_POLICY_CONDITION_TYPE_H

#include <optional>
#include <string>
#include <string_view>

namespace sea::domain::access_control {

/**
 * Type d'une PolicyCondition.
 *
 * Predicate : feuille de l'arbre, contient une comparaison atomique
 * All       : nœud composite, TOUS les enfants doivent être true (AND)
 * Any       : nœud composite, AU MOINS UN enfant doit être true (OR)
 * Not       : nœud négation, doit avoir exactement 1 enfant
 */
enum class PolicyConditionType {
    Predicate,
    All,
    Any,
    Not
};

std::string_view to_string(PolicyConditionType t) noexcept;

std::optional<PolicyConditionType> policy_condition_type_from_string(
    const std::string& s) noexcept;

} // namespace sea::domain::access_control

#endif // SEA_DOMAIN_ACCESS_CONTROL_POLICY_CONDITION_TYPE_H