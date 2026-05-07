#include "intersects_strategy.h"
namespace sea::application::access_control {

bool IntersectsStrategy::evaluate(const ResolvedValue& l, const ResolvedValue& r) const
{
    if (l.is_list() && r.is_list()) {
        for (const auto& item : *l.list) {
            if (std::find(r.list->begin(), r.list->end(), item) != r.list->end()) {
                return true;
            }
        }
        return false;
    }
    // Tolérance : scalar in list
    if (l.is_scalar() && r.is_list()) {
        return std::find(r.list->begin(), r.list->end(), *l.scalar) != r.list->end();
    }
    return false;
}
}