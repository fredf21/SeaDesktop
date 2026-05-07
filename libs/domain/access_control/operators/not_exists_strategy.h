#ifndef NOT_EXISTS_STRATEGY_H
#define NOT_EXISTS_STRATEGY_H
#include "operator_strategy.h"

namespace sea::application::access_control {
class NotExistsStrategy : public OperatorStrategy {
public:
    bool evaluate(const ResolvedValue& l, const ResolvedValue& r) const override;
    std::string_view name() const noexcept override { return "not_exists"; }
    bool ignores_right_operand() const noexcept override { return true; }
};
}
#endif // NOT_EXISTS_STRATEGY_H
