#include "not_in_strategy.h"
namespace sea::application::access_control {
bool NotInStrategy::evaluate(const ResolvedValue& l, const ResolvedValue& r) const
{
    if (l.is_scalar() && r.is_list()) {
        return std::find(r.list->begin(), r.list->end(), *l.scalar) == r.list->end();
    }
    return true;  // si left n'est pas scalar ou right pas list, on considère "pas in"
}
}