#ifndef LESS_THAN_STRATEGY_H
#define LESS_THAN_STRATEGY_H

#include <string_view>
#include "operator_strategy.h"
namespace sea::application::access_control {

class LessThanStrategy : public OperatorStrategy {
public:
    bool evaluate(const ResolvedValue& l, const ResolvedValue& r) const override;
    std::string_view name() const noexcept override { return "less_than"; }
};
} // namespace

#endif // LESS_THAN_STRATEGY_H
