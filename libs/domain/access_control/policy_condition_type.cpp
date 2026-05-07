#include "policy_condition_type.h"

#include <algorithm>
#include <cctype>

namespace sea::domain::access_control {

namespace {

std::string to_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

} // namespace anonyme

std::string_view to_string(PolicyConditionType t) noexcept
{
    switch (t) {
    case PolicyConditionType::Predicate: return "predicate";
    case PolicyConditionType::All:       return "all";
    case PolicyConditionType::Any:       return "any";
    case PolicyConditionType::Not:       return "not";
    }
    return "unknown";
}

std::optional<PolicyConditionType> policy_condition_type_from_string(
    const std::string& s) noexcept
{
    const auto lower = to_lower(s);

    if (lower == "predicate")               return PolicyConditionType::Predicate;
    if (lower == "all" || lower == "and")   return PolicyConditionType::All;
    if (lower == "any" || lower == "or")    return PolicyConditionType::Any;
    if (lower == "not")                     return PolicyConditionType::Not;

    return std::nullopt;
}

} // namespace sea::domain::access_control