#pragma once

#include "../field_type.h"

#include <string_view>

// ─────────────────────────────────────────────────────────────
// Mapping FieldType → PostgreSQL types
//
// Utilise pour generer le SQL pour PostgreSQL.
// (Pas encore implementee dans le MVP, mais le mapping est pret.)
// ─────────────────────────────────────────────────────────────

namespace sea::domain {

constexpr std::string_view to_postgres_type(FieldType t) noexcept {
    switch (t) {
    case FieldType::String:    return "TEXT";
    case FieldType::Text:      return "TEXT";
    case FieldType::Int:       return "INTEGER";
    case FieldType::Float:     return "DOUBLE PRECISION";
    case FieldType::Bool:      return "BOOLEAN";
    case FieldType::Timestamp: return "TIMESTAMP";
    case FieldType::UUID:      return "UUID";

    // Types métier stockés comme texte
    case FieldType::Password:  return "TEXT";
    case FieldType::Email:     return "TEXT";

    default:                   return "TEXT";
    }
}

} // namespace sea::domain
