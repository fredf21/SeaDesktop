#include "exists_strategy.h"

namespace sea::application::access_control {

bool ExistsStrategy::evaluate(const ResolvedValue& l, const ResolvedValue& /*r*/) const
{
    return !l.is_empty();
}

} // namespace