#ifndef SEA_DOMAIN_ACCESS_CONTROL_POLICY_CONDITION_H
#define SEA_DOMAIN_ACCESS_CONTROL_POLICY_CONDITION_H

#include "policy_condition_type.h"
#include "policy_predicate.h"

#include <optional>
#include <vector>

namespace sea::domain::access_control {

/**
 * Condition d'autorisation, possiblement composée (AND/OR/NOT).
 *
 * C'est un arbre. Chaque nœud est soit un Predicate (feuille),
 * soit un opérateur logique (All/Any/Not) avec des enfants.
 *
 * Exemples :
 *
 * 1. Simple predicate :
 *    type      = Predicate
 *    predicate = { subject.roles, contains, "admin" }
 *    children  = []
 *
 * 2. AND de 2 conditions :
 *    type      = All
 *    predicate = nullopt
 *    children  = [ predicate1, predicate2 ]
 *
 * 3. (admin) OR (manager AND same_dept) :
 *    type = Any
 *    children = [
 *      { type=Predicate, predicate=(subject.roles contains "admin") },
 *      { type=All, children=[
 *          { type=Predicate, predicate=(subject.roles contains "manager") },
 *          { type=Predicate, predicate=(subject.dept == resource.dept) }
 *        ]
 *      }
 *    ]
 */
class PolicyCondition {
public:
    PolicyCondition() = default;

    // ─── Constructeurs ───

    explicit PolicyCondition(PolicyPredicate predicate);

    static PolicyCondition all_of(std::vector<PolicyCondition> children);
    static PolicyCondition any_of(std::vector<PolicyCondition> children);
    static PolicyCondition not_of(PolicyCondition child);

    // ─── Accesseurs ───

    PolicyConditionType type() const noexcept { return type_; }

    const std::optional<PolicyPredicate>& predicate() const noexcept {
        return predicate_;
    }

    const std::vector<PolicyCondition>& children() const noexcept {
        return children_;
    }

    bool is_empty() const noexcept;

    // ─── Builders fluides (utiles pour les tests et le shortcut compiler) ───

    void add_child(PolicyCondition child);

    // Validation structurelle (lance std::invalid_argument si invalide)
    void validate() const;

private:
    PolicyConditionType type_ = PolicyConditionType::Predicate;
    std::optional<PolicyPredicate> predicate_;
    std::vector<PolicyCondition> children_;
};

} // namespace sea::domain::access_control

#endif // SEA_DOMAIN_ACCESS_CONTROL_POLICY_CONDITION_H