#pragma once

#include "field_type.h"

#include <cstddef>    // size_t
#include <cstdint>    // int64_t
#include <optional>
#include <string>
#include <utility>    // std::move
#include <variant>

namespace sea::domain {

// ─────────────────────────────────────────────────────────────
// Valeur par défaut possible pour un champ
// std::monostate = aucun default défini
// ─────────────────────────────────────────────────────────────
using DefaultValue = std::variant<
    std::monostate,
    std::string,
    int64_t,
    double,
    bool
    >;

// ─────────────────────────────────────────────────────────────
// Contraintes numériques génériques
// Permet de supporter à la fois Int et Float
// ─────────────────────────────────────────────────────────────
using NumericConstraint = std::variant<int64_t, double>;

// ─────────────────────────────────────────────────────────────
// Représente un champ appartenant à une entité métier
// Exemple :
//   User.email
//   Product.price
//   Order.created_at
// ─────────────────────────────────────────────────────────────
struct Field {
    std::string name;            // ex: "email"
    FieldType   type;            // ex: FieldType::Email

    // Contraintes principales
    bool required     = true;    // champ obligatoire
    bool unique       = false;   // contrainte d'unicité
    bool indexed      = false;   // index DB conseillé

    // Valeur par défaut éventuelle
    DefaultValue default_val = std::monostate{};

    // Contraintes optionnelles
    std::optional<size_t> max_length;                // String / Text
    std::optional<NumericConstraint> min_value;      // Int / Float
    std::optional<NumericConstraint> max_value;      // Int / Float

    // Indique si le champ peut apparaître dans les réponses JSON
    // Exemple : password → false
    bool serializable = true;

    [[nodiscard]] bool has_default() const noexcept {
        return !std::holds_alternative<std::monostate>(default_val);
    }
};

// ─────────────────────────────────────────────────────────────
// Helpers de construction (style fluide)
// Très utile pour construire un schéma en C++
// ─────────────────────────────────────────────────────────────

inline Field make_field(std::string name, FieldType type) {
    return Field{
        .name = std::move(name),
        .type = type
    };
}

inline Field& required(Field& f, bool v = true) {
    f.required = v;
    return f;
}

inline Field& unique(Field& f, bool v = true) {
    f.unique = v;
    return f;
}

inline Field& indexed(Field& f, bool v = true) {
    f.indexed = v;
    return f;
}

inline Field& hidden(Field& f) {
    f.serializable = false;
    return f;
}

inline Field& max_length(Field& f, size_t n) {
    f.max_length = n;
    return f;
}

inline Field& min_value(Field& f, int64_t v) {
    f.min_value = v;
    return f;
}

inline Field& min_value(Field& f, double v) {
    f.min_value = v;
    return f;
}

inline Field& max_value(Field& f, int64_t v) {
    f.max_value = v;
    return f;
}

inline Field& max_value(Field& f, double v) {
    f.max_value = v;
    return f;
}

inline Field& default_value(Field& f, std::string v) {
    f.default_val = std::move(v);
    return f;
}

inline Field& default_value(Field& f, int64_t v) {
    f.default_val = v;
    return f;
}

inline Field& default_value(Field& f, double v) {
    f.default_val = v;
    return f;
}

inline Field& default_value(Field& f, bool v) {
    f.default_val = v;
    return f;
}

} // namespace sea::domain