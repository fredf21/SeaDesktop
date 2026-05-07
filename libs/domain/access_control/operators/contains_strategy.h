#ifndef CONTAINS_STRATEGY_H
#define CONTAINS_STRATEGY_H
#include <string_view>
#include "operator_strategy.h"
namespace sea::application::access_control {

class ContainsStrategy : public OperatorStrategy {
public:
    bool evaluate(const ResolvedValue& l, const ResolvedValue& r) const override;
    std::string_view name() const noexcept override { return "contains"; }
};

} // namespace

#endif // CONTAINS_STRATEGY_H
