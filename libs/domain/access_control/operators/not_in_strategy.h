#ifndef NOT_IN_STRATEGY_H
#define NOT_IN_STRATEGY_H

#include "operator_strategy.h"
namespace sea::application::access_control {

class NotInStrategy : public OperatorStrategy {
public:
    bool evaluate(const ResolvedValue& l, const ResolvedValue& r) const override;
    std::string_view name() const noexcept override { return "not_in"; }
};
} // namespace

#endif // NOT_IN_STRATEGY_H
