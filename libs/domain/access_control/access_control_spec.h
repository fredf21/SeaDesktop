#ifndef SEA_DOMAIN_ACCESS_CONTROL_ACCESS_CONTROL_SPEC_H
#define SEA_DOMAIN_ACCESS_CONTROL_ACCESS_CONTROL_SPEC_H

#include "policy_condition.h"

namespace sea::domain::access_control {

/**
 * Spécification d'autorisation pour UNE opération sur UNE entité.
 *
 * Contient une PolicyCondition (potentiellement composée) qui doit
 * être évaluée à TRUE pour que l'opération soit autorisée.
 *
 * Exemple : Employee.update
 *   condition = All [
 *     subject.roles intersects [admin, manager],
 *     subject.dept_id == resource.dept_id
 *   ]
 *
 * Note sur le moment d'évaluation :
 *   - Si la condition n'utilise QUE subject + context → évaluable AVANT le handler
 *   - Si la condition utilise resource → évaluable APRÈS le chargement de la ressource
 *
 * La méthode `requires_resource()` indique si l'évaluation a besoin de
 * la ressource. Le middleware peut alors décider :
 *   - Évaluation pré-handler (subject-only) : rapide, court-circuite si KO
 *   - Évaluation post-handler (resource-aware) : après le find_by_id()
 */
class AccessControlSpec {
public:
    AccessControlSpec() = default;
    explicit AccessControlSpec(PolicyCondition condition);

    const PolicyCondition& condition() const noexcept { return condition_; }
    void set_condition(PolicyCondition c) { condition_ = std::move(c); }

    // True si la spec contient au moins une référence à `resource.*`
    // → l'évaluation doit se faire APRÈS chargement de la ressource
    bool requires_resource() const noexcept;

    // True si la condition est vide (pas de règle = autorisation libre)
    bool is_empty() const noexcept { return condition_.is_empty(); }

    // Valide structurellement la condition (lance si invalide)
    void validate() const { condition_.validate(); }

private:
    PolicyCondition condition_;
};

} // namespace sea::domain::access_control

#endif // SEA_DOMAIN_ACCESS_CONTROL_ACCESS_CONTROL_SPEC_H