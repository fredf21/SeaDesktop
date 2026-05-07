#ifndef ENDS_WITH_STRATEGY_H
#define ENDS_WITH_STRATEGY_H

#include "operator_strategy.h"

namespace sea::application::access_control {

class EndsWithStrategy : public OperatorStrategy {
public:
    bool evaluate(const ResolvedValue& l, const ResolvedValue& r) const override;
    std::string_view name() const noexcept override { return "ends_with"; }
};

} // namespace

#endif // ENDS_WITH_STRATEGY_H
