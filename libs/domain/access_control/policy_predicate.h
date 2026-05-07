#ifndef SEA_DOMAIN_ACCESS_CONTROL_POLICY_PREDICATE_H
#define SEA_DOMAIN_ACCESS_CONTROL_POLICY_PREDICATE_H

#include "policy_operator.h"
#include "policy_value_ref.h"

namespace sea::domain::access_control {

/**
 * Condition atomique ABAC.
 *
 * Compare deux valeurs avec un opérateur.
 *
 * Exemple : "subject.attributes.department_id == resource.attributes.department_id"
 *   → left  = PolicyValueRef::from_subject("attributes.department_id")
 *   → op    = PolicyOperator::Equals
 *   → right = PolicyValueRef::from_resource("attributes.department_id")
 *
 * Exemple : "subject.roles contains 'manager'"
 *   → left  = PolicyValueRef::from_subject("roles")
 *   → op    = PolicyOperator::Contains
 *   → right = PolicyValueRef::from_literal("manager")
 *
 * Exemple : "context.time.hour >= 8"
 *   → left  = PolicyValueRef::from_context("time.hour")
 *   → op    = PolicyOperator::GreaterThanOrEqual
 *   → right = PolicyValueRef::from_literal("8")
 */
struct PolicyPredicate {
    PolicyValueRef left;
    PolicyOperator op = PolicyOperator::Equals;
    PolicyValueRef right;

    // Factory rapide pour les cas courants
    static PolicyPredicate make(
        PolicyValueRef l,
        PolicyOperator o,
        PolicyValueRef r)
    {
        PolicyPredicate p;
        p.left = std::move(l);
        p.op = o;
        p.right = std::move(r);
        return p;
    }
};

} // namespace sea::domain::access_control

#endif // SEA_DOMAIN_ACCESS_CONTROL_POLICY_PREDICATE_H