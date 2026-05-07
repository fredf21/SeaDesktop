#ifndef INTERSECTS_STRATEGY_H
#define INTERSECTS_STRATEGY_H

#include <string_view>
#include "operator_strategy.h"
namespace sea::application::access_control {

class IntersectsStrategy : public OperatorStrategy {
public:
    bool evaluate(const ResolvedValue& l, const ResolvedValue& r) const override;
    std::string_view name() const noexcept override { return "intersects"; }
};
} // namespace

#endif // INTERSECTS_STRATEGY_H
