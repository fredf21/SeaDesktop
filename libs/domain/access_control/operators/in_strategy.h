#ifndef SEA_APPLICATION_ACCESS_CONTROL_OPERATORS_IN_STRATEGY_H
#define SEA_APPLICATION_ACCESS_CONTROL_OPERATORS_IN_STRATEGY_H

#include "operator_strategy.h"

namespace sea::application::access_control {

/**
 * Vérifie qu'une valeur scalaire est dans une liste.
 *
 * "manager" In ["admin", "manager", "user"] → true
 * "guest"   In ["admin", "manager", "user"] → false
 */
class InStrategy : public OperatorStrategy {
public:
    bool evaluate(
        const ResolvedValue& left,
        const ResolvedValue& right
        ) const override;

    std::string_view name() const noexcept override { return "in"; }
};

} // namespace
#endif