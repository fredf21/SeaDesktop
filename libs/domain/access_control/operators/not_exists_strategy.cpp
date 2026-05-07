#include "not_exists_strategy.h"
namespace sea::application::access_control {

bool NotExistsStrategy::evaluate(const ResolvedValue& l, const ResolvedValue& /*r*/) const
{
    return l.is_empty();
}
}