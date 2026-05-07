#include "policy_operator.h"

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

std::string_view to_string(PolicyOperator op) noexcept
{
    switch (op) {
    case PolicyOperator::Equals:             return "equals";
    case PolicyOperator::NotEquals:          return "not_equals";
    case PolicyOperator::In:                 return "in";
    case PolicyOperator::NotIn:              return "not_in";
    case PolicyOperator::Contains:           return "contains";
    case PolicyOperator::StartsWith:         return "starts_with";
    case PolicyOperator::EndsWith:           return "ends_with";
    case PolicyOperator::GreaterThan:        return "greater_than";
    case PolicyOperator::GreaterThanOrEqual: return "greater_than_or_equal";
    case PolicyOperator::LessThan:           return "less_than";
    case PolicyOperator::LessThanOrEqual:    return "less_than_or_equal";
    case PolicyOperator::Exists:             return "exists";
    case PolicyOperator::NotExists:          return "not_exists";
    case PolicyOperator::RegexMatch:         return "regex_match";
    case PolicyOperator::Intersects:         return "intersects";
    }
    return "unknown";
}

std::optional<PolicyOperator> policy_operator_from_string(
    const std::string& s) noexcept
{
    const auto lower = to_lower(s);

    // Aliases courts et longs
    if (lower == "equals" || lower == "eq" || lower == "==") {
        return PolicyOperator::Equals;
    }
    if (lower == "not_equals" || lower == "ne" || lower == "!=") {
        return PolicyOperator::NotEquals;
    }
    if (lower == "in") {
        return PolicyOperator::In;
    }
    if (lower == "not_in" || lower == "notin") {
        return PolicyOperator::NotIn;
    }
    if (lower == "contains") {
        return PolicyOperator::Contains;
    }
    if (lower == "starts_with" || lower == "startswith") {
        return PolicyOperator::StartsWith;
    }
    if (lower == "ends_with" || lower == "endswith") {
        return PolicyOperator::EndsWith;
    }
    if (lower == "greater_than" || lower == "gt" || lower == ">") {
        return PolicyOperator::GreaterThan;
    }
    if (lower == "greater_than_or_equal" || lower == "gte" || lower == ">=") {
        return PolicyOperator::GreaterThanOrEqual;
    }
    if (lower == "less_than" || lower == "lt" || lower == "<") {
        return PolicyOperator::LessThan;
    }
    if (lower == "less_than_or_equal" || lower == "lte" || lower == "<=") {
        return PolicyOperator::LessThanOrEqual;
    }
    if (lower == "exists") {
        return PolicyOperator::Exists;
    }
    if (lower == "not_exists" || lower == "notexists") {
        return PolicyOperator::NotExists;
    }
    if (lower == "regex_match" || lower == "regex" || lower == "matches") {
        return PolicyOperator::RegexMatch;
    }
    if (lower == "intersects") {
        return PolicyOperator::Intersects;
    }

    return std::nullopt;
}

} // namespace sea::domain::access_control