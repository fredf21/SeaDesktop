#pragma once

#include "field_type.h"
#include "file_field_config.h"

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
    std::int64_t,
    std::uint64_t,
    std::int32_t,
    std::uint32_t,
    std::int16_t,
    std::uint16_t,
    double,
    bool
    >;

enum class DatabaseDialect {
    MySQL,
    PostgreSQL,
    SQLite,
    SQLServer
};

struct NativeDbType {
    DatabaseDialect dialect;
    std::string type_name; // ex: "MEDIUMINT UNSIGNED", "JSONB", "MONEY"
};

// ─────────────────────────────────────────────────────────────
// Contraintes numériques génériques
// Permet de supporter à la fois Int et Float
// ─────────────────────────────────────────────────────────────
using NumericConstraint = std::variant<std::int64_t, std::int32_t, std::uint32_t, std::uint64_t, double>;

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
    bool unsigned_value = false; // pour les type numeric

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

    std::optional<NativeDbType> native_type; // Pas compatible avec les base de donnees NoSQL

    // annotation explicite pour rename de colonne
    //
    // Si renseigne, le bootstrapper saura qu'il s'agit d'un rename
    // (et non d'un drop + add) et utilisera ALTER TABLE CHANGE COLUMN
    // pour preserver les donnees.
    //
    // Exemple :
    //   - name: phone_number
    //     type: string
    //     previous_name: phone     ← NEW
    std::optional<std::string> previous_name;

    // ─────────────────────────────────────────────────────────
    // Configuration spécifique aux champs File.
    //
    // Renseignée si et seulement si type == FieldType::File.
    // Contient les contraintes d'upload (max_size, mime, extensions),
    // le sous-dossier de stockage, et le comportement on_delete.
    //
    // Côté colonne SQL, un champ File se traduit par un BINARY(16)
    // référençant sea_files.id via une FK.
    // ─────────────────────────────────────────────────────────
    std::optional<FileFieldConfig> file_config;

    [[nodiscard]] bool has_default() const noexcept {
        return !std::holds_alternative<std::monostate>(default_val);
    }

    [[nodiscard]] bool has_previous_name() const noexcept {
        return previous_name.has_value() && !previous_name->empty();
    }
    // Indique si le champ est un upload File correctement configuré.
    // Note : type==File implique normalement file_config.has_value()
    // (garanti par le schema_validator à l'Étape 3), mais on reste
    // défensif ici pour éviter un crash côté handlers/codegen.
    [[nodiscard]] bool is_file_field() const noexcept {
        return type == FieldType::File && file_config.has_value();
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
inline Field& min_value(Field& f, uint64_t v) {
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
inline Field& max_value(Field& f, uint64_t v) {
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
inline Field& default_value(Field& f, uint64_t v) {
    f.default_val = v;
    return f;
}
inline Field& default_value(Field& f, int32_t v) {
    f.default_val = v;
    return f;
}
inline Field& default_value(Field& f, uint32_t v) {
    f.default_val = v;
    return f;
}
inline Field& default_value(Field& f, int16_t v) {
    f.default_val = v;
    return f;
}
inline Field& default_value(Field& f, uint16_t v) {
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
inline Field& renamed_from(Field& f, std::string old_name) {
    f.previous_name = std::move(old_name);
    return f;
}
// ─────────────────────────────────────────────────────────────
// Helper fluide pour configurer un champ File.
// Force type = FieldType::File et installe la config.
//
// Exemple d'usage C++ :
//   auto f = make_field("avatar", FieldType::File);
//   as_file(f, FileFieldConfig{
//       .max_size_bytes = 5 * 1024 * 1024,
//       .allowed_mime_types = {"image/png", "image/jpeg"},
//       .storage_path = "users/avatars",
//       .on_delete = OnDeleteFile::Cascade
//   });
// ─────────────────────────────────────────────────────────────
inline Field& as_file(Field& f, FileFieldConfig cfg) {
    f.type = FieldType::File;
    f.file_config = std::move(cfg);
    return f;
}

} // namespace sea::domain