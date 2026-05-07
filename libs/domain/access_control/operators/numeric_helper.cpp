#include "numeric_helper.h"

namespace sea::application::access_control {

std::optional<double> NumericHelper::parse(const std::string& s) noexcept
{
    try {
        std::size_t pos = 0;
        double v = std::stod(s, &pos);
        if (pos != s.size()) return std::nullopt;
        return v;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::pair<double, double>> NumericHelper::parse_both(
    const ResolvedValue& left,
    const ResolvedValue& right) noexcept
{
    if (!left.is_scalar() || !right.is_scalar()) return std::nullopt;

    const auto l = parse(*left.scalar);
    const auto r = parse(*right.scalar);

    if (!l.has_value() || !r.has_value()) return std::nullopt;

    return std::make_pair(*l, *r);
}

} // namespace