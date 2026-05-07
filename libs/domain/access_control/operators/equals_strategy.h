#ifndef SEA_APPLICATION_ACCESS_CONTROL_OPERATORS_EQUALS_STRATEGY_H
#define SEA_APPLICATION_ACCESS_CONTROL_OPERATORS_EQUALS_STRATEGY_H

#include "operator_strategy.h"

namespace sea::application::access_control {

/**
 * Égalité stricte de valeurs.
 *
 * Comportement :
 *   - scalar == scalar : comparaison de strings
 *   - list == list     : comparaison ordre + contenu
 *   - empty == empty   : true (cas limite)
 *   - mixed types      : false
 */
class EqualsStrategy : public OperatorStrategy {
public:
    bool evaluate(
        const ResolvedValue& left,
        const ResolvedValue& right
        ) const override;

    std::string_view name() const noexcept override { return "equals"; }
};

} // namespace sea::application::access_control

#endif