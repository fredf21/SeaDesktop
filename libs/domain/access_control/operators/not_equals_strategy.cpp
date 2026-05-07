#include "not_equals_strategy.h"
namespace sea::application::access_control {

bool NotEqualsStrategy::evaluate(const ResolvedValue& l, const ResolvedValue& r) const
{
    if (l.is_scalar() && r.is_scalar()) return *l.scalar != *r.scalar;
    if (l.is_empty() && r.is_empty())   return false;
    if (l.is_list() && r.is_list())     return *l.list != *r.list;
    return true;  // types différents = différents
}
} // namespace