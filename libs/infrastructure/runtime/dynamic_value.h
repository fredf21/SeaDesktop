#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <variant>
#include <vector>

namespace sea::infrastructure::runtime {

// ─────────────────────────────────────────────────────────────
// Valeur dynamique manipulée par le runtime générique
//
// Sert à représenter un champ à l’exécution, sans classe C++
// spécialisée par entité.
//
// Exemple :
//   "email" -> std::string
//   "age"   -> int64_t
//   "score" -> double
//   "admin" -> bool
// ─────────────────────────────────────────────────────────────
struct NativeValue {
    std::string dialect;
    std::string sql_type;

    nlohmann::json value;
    bool operator==(const NativeValue&) const = default;
};
using DynamicValue = std::variant<
    std::monostate,
    std::string,
    std::int64_t,
    double,
    bool,
    std::vector<std::string>,
    std::vector<std::int64_t>,
    nlohmann::json,
    std::vector<std::uint8_t>,
    NativeValue
    >;

} // namespace sea::infrastructure::runtime