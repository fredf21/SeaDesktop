#include "less_than_or_equal_strategy.h"

#include "numeric_helper.h"

namespace sea::application::access_control {

bool LessThanOrEqualStrategy::evaluate(const ResolvedValue& l, const ResolvedValue& r) const
{
    auto values = NumericHelper::parse_both(l, r);
    return values.has_value() && values->first <= values->second;
}
} // namespace