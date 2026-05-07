#include "starts_with_strategy.h"
namespace sea::application::access_control {

bool StartsWithStrategy::evaluate(const ResolvedValue& l, const ResolvedValue& r) const
{
    if (!l.is_scalar() || !r.is_scalar()) return false;
    if (r.scalar->size() > l.scalar->size()) return false;
    return std::equal(r.scalar->begin(), r.scalar->end(), l.scalar->begin());
}
}