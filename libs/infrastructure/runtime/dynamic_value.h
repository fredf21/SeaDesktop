#pragma once

#include <cstdint>
#include <string>
#include <variant>

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
using DynamicValue = std::variant<
    std::monostate,
    std::string,
    std::int64_t,
    double,
    bool
    >;

} // namespace sea::infrastructure::runtime