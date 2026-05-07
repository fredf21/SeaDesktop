#include "regex_match_strategy.h"

namespace sea::application::access_control {

RegexMatchStrategy::RegexMatchStrategy(
    std::unordered_map<std::string, std::regex> regex_cache)
    : regex_cache_(std::move(regex_cache))
{
}

bool RegexMatchStrategy::evaluate(
    const ResolvedValue& left,
    const ResolvedValue& right) const
{
    if (!left.is_scalar() || !right.is_scalar()) {
        return false;
    }

    auto it = regex_cache_.find(*right.scalar);
    if (it == regex_cache_.end()) {
        return false;  // pattern non pré-compilé
    }

    try {
        return std::regex_search(*left.scalar, it->second);
    } catch (...) {
        return false;
    }
}

} // namespace
