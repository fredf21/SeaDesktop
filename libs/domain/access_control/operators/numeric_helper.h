#ifndef SEA_APPLICATION_ACCESS_CONTROL_OPERATORS_NUMERIC_HELPER_H
#define SEA_APPLICATION_ACCESS_CONTROL_OPERATORS_NUMERIC_HELPER_H

#include "../value_resolver.h"

#include <optional>
#include <string>
#include <utility>

namespace sea::application::access_control {

/**
 * Helpers pour les opérateurs numériques.
 */
class NumericHelper {
public:
    /**
     * Parse un string en double. Retourne nullopt si invalide.
     */
    static std::optional<double> parse(const std::string& s) noexcept;

    /**
     * Parse les deux operands en numbers.
     * Retourne nullopt si l'un des deux est invalide ou non scalaire.
     */
    static std::optional<std::pair<double, double>> parse_both(
        const ResolvedValue& left,
        const ResolvedValue& right
        ) noexcept;
};

} // namespace
#endif
