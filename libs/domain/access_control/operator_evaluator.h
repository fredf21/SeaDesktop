#ifndef SEA_APPLICATION_ACCESS_CONTROL_OPERATOR_EVALUATOR_H
#define SEA_APPLICATION_ACCESS_CONTROL_OPERATOR_EVALUATOR_H

#include "access_control/policy_operator.h"
#include "operators/operator_registry.h"
#include "value_resolver.h"

namespace sea::application::access_control {

/**
 * Façade qui délègue l'évaluation à la stratégie appropriée
 * dans le registre.
 *
 * Si l'opérateur n'a pas de stratégie enregistrée, retourne false.
 */
class OperatorEvaluator {
public:
    explicit OperatorEvaluator(const OperatorRegistry& registry);

    bool evaluate(
        sea::domain::access_control::PolicyOperator op,
        const ResolvedValue& left,
        const ResolvedValue& right
        ) const;

private:
    const OperatorRegistry& registry_;
};

} // namespace
#endif