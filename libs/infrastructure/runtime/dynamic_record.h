#pragma once

#include "dynamic_value.h"

#include <string>
#include <unordered_map>

namespace sea::infrastructure::runtime {

// ─────────────────────────────────────────────────────────────
// Enregistrement dynamique
//
// Représente une ligne logique / document logique d’une entité.
// Exemple :
//   {
//     "id": "uuid-123",
//     "email": "test@example.com",
//     "age": 22
//   }
// ─────────────────────────────────────────────────────────────
using DynamicRecord = std::unordered_map<std::string, DynamicValue>;

} // namespace sea::infrastructure::runtime