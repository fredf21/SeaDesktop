#include "abac_mode.h"

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

std::optional<AbacMode> abac_mode_from_string(const std::string& s) noexcept
{
    const auto lower = to_lower(s);

    if (lower == "permissive") return AbacMode::Permissive;
    if (lower == "strict")     return AbacMode::Strict;

    return std::nullopt;
}

std::string_view to_string(AbacMode mode) noexcept
{
    switch (mode) {
    case AbacMode::Permissive: return "permissive";
    case AbacMode::Strict:     return "strict";
    }
    return "unknown";
}

} // namespace sea::domain::access_control