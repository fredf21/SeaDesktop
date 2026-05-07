#ifndef GREATER_THAN_OR_EQUAL_STRATEGY_H
#define GREATER_THAN_OR_EQUAL_STRATEGY_H

#include "operator_strategy.h"
namespace sea::application::access_control {

class GreaterThanOrEqualStrategy : public OperatorStrategy {
public:
    bool evaluate(const ResolvedValue& l, const ResolvedValue& r) const override;
    std::string_view name() const noexcept override { return "greater_than_or_equal"; }
};
} // namespace

#endif // GREATER_THAN_OR_EQUAL_STRATEGY_H
