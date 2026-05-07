#include "policy_value_source.h"

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

std::string_view to_string(PolicyValueSource s) noexcept
{
    switch (s) {
    case PolicyValueSource::Literal:  return "literal";
    case PolicyValueSource::Subject:  return "subject";
    case PolicyValueSource::Resource: return "resource";
    case PolicyValueSource::Context:  return "context";
    }
    return "unknown";
}

std::optional<PolicyValueSource> policy_value_source_from_string(
    const std::string& s) noexcept
{
    const auto lower = to_lower(s);

    if (lower == "literal")  return PolicyValueSource::Literal;
    if (lower == "subject")  return PolicyValueSource::Subject;
    if (lower == "resource") return PolicyValueSource::Resource;
    if (lower == "context")  return PolicyValueSource::Context;

    return std::nullopt;
}

} // namespace sea::domain::access_control