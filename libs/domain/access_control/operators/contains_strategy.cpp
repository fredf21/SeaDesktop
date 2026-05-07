#include "contains_strategy.h"
namespace sea::application::access_control {
bool ContainsStrategy::evaluate(const ResolvedValue& l, const ResolvedValue& r) const
{
    // List contains scalar
    if (l.is_list() && r.is_scalar()) {
        return std::find(l.list->begin(), l.list->end(), *r.scalar) != l.list->end();
    }
    // String contains substring
    if (l.is_scalar() && r.is_scalar()) {
        return l.scalar->find(*r.scalar) != std::string::npos;
    }
    return false;
}
}