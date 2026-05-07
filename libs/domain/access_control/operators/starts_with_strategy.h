#ifndef STARTS_WITH_STRATEGY_H
#define STARTS_WITH_STRATEGY_H

#include "operator_strategy.h"
namespace sea::application::access_control {

class StartsWithStrategy : public OperatorStrategy {
public:
    bool evaluate(const ResolvedValue& l, const ResolvedValue& r) const override;
    std::string_view name() const noexcept override { return "starts_with"; }
};
} // namespace
#endif // STARTS_WITH_STRATEGY_H
