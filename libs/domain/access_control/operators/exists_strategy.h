#ifndef EXISTS_STRATEGY_H
#define EXISTS_STRATEGY_H
#include "operator_strategy.h"

namespace sea::application::access_control {
class ExistsStrategy : public OperatorStrategy {
public:
    bool evaluate(const ResolvedValue& l, const ResolvedValue& r) const override;
    std::string_view name() const noexcept override { return "exists"; }
    bool ignores_right_operand() const noexcept override { return true; }
};
}
#endif // EXISTS_STRATEGY_H
