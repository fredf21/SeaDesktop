#include "in_strategy.h"
#include <algorithm>

namespace sea::application::access_control {

bool InStrategy::evaluate(
    const ResolvedValue& left,
    const ResolvedValue& right) const
{
    if (left.is_scalar() && right.is_list()) {
        return std::find(right.list->begin(), right.list->end(), *left.scalar)
        != right.list->end();
    }
    return false;
}

} // namespace