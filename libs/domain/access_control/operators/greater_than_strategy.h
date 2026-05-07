#ifndef GREATER_THAN_STRATEGY_H
#define GREATER_THAN_STRATEGY_H

#include <string_view>
#include "operator_strategy.h"
namespace sea::application::access_control {

class GreaterThanStrategy : public OperatorStrategy {
public:
    bool evaluate(const ResolvedValue& l, const ResolvedValue& r) const override;
    std::string_view name() const noexcept override { return "greater_than"; }
};
} // namespace
#endif // GREATER_THAN_STRATEGY_H
