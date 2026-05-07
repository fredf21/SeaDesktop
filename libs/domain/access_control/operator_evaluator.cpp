#include "operator_evaluator.h"

namespace sea::application::access_control {

using sea::domain::access_control::PolicyOperator;

OperatorEvaluator::OperatorEvaluator(const OperatorRegistry& registry)
    : registry_(registry)
{
}

bool OperatorEvaluator::evaluate(
    PolicyOperator op,
    const ResolvedValue& left,
    const ResolvedValue& right) const
{
    const auto* strategy = registry_.find(op);
    if (!strategy) {
        // Pas de stratégie pour cet opérateur (ne devrait jamais arriver
        // si on utilise create_default)
        return false;
    }

    // Cas spéciaux : Exists/NotExists ignorent right
    // (mais on délègue tout à la stratégie qui sait quoi faire)

    // Cas général : si left est vide et que la stratégie a besoin de left,
    // on retourne false (sauf NotExists qui sait gérer)
    if (left.is_empty() && !strategy->ignores_right_operand()
        && op != PolicyOperator::NotExists
        && op != PolicyOperator::NotEquals) {
        return false;
    }

    return strategy->evaluate(left, right);
}

} // namespace