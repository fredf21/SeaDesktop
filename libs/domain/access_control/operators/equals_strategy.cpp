#include "equals_strategy.h"

namespace sea::application::access_control {

bool EqualsStrategy::evaluate(
    const ResolvedValue& left,
    const ResolvedValue& right) const
{
    if (left.is_scalar() && right.is_scalar()) {
        return *left.scalar == *right.scalar;
    }
    if (left.is_empty() && right.is_empty()) {
        return true;
    }
    if (left.is_list() && right.is_list()) {
        return *left.list == *right.list;
    }
    return false;
}

} // namespace