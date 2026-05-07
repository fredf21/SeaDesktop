#ifndef NOT_EQUALS_STRATEGY_H
#define NOT_EQUALS_STRATEGY_H

#include <string_view>
#include "operator_strategy.h"
namespace sea::application::access_control {

class NotEqualsStrategy : public OperatorStrategy {
public:
    bool evaluate(const ResolvedValue& l, const ResolvedValue& r) const override;
    std::string_view name() const noexcept override { return "not_equals"; }
};
} // namespace
#endif // NOT_EQUALS_STRATEGY_H
