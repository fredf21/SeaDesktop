#pragma once

// ─────────────────────────────────────────────────────────────
// Standard includes
// ─────────────────────────────────────────────────────────────

#include <algorithm>    // std::transform
#include <cctype>       // std::tolower
#include <optional>     // std::optional
#include <string>       // std::string
#include <string_view>  // std::string_view

// ─────────────────────────────────────────────────────────────
// Domaine métier : définition des types de champs
// ─────────────────────────────────────────────────────────────

namespace sea::domain {

// Représente les types de données supportés dans ton DSL.
// Ces types sont utilisés :
// - dans le YAML
// - dans le runtime générique
// - plus tard dans le code generator (SQL / C++)
// - dans la validation des données
enum class FieldType {
    // Types simples
    String,     // texte court
    Int,        // entier
    Float,      // nombre décimal
    Bool,       // booléen

    // Types système
    Timestamp,  // date/heure
    UUID,       // identifiant unique

    // Types avancés portables
    BigInt,
    SmallInt,
    Decimal,
    Json,
    Binary,

    // Types métier enrichis (logique supplémentaire)
    Password,   // stocké hashé automatiquement
    Email,      // validé selon format email

    // Texte long (équivalent TEXT en SQL)
    Text,

    // Échappatoire pour types spécifiques DB
    Native
};

// ─────────────────────────────────────────────────────────────
// Conversion enum → string
// Utilisé pour :
// - debug
// - logs
// - génération de code
// ─────────────────────────────────────────────────────────────

constexpr std::string_view to_string(FieldType t) noexcept {
    switch (t) {
    case FieldType::String:    return "string";
    case FieldType::Int:       return "int";
    case FieldType::Float:     return "float";
    case FieldType::Bool:      return "bool";
    case FieldType::Timestamp: return "timestamp";
    case FieldType::UUID:      return "uuid";
    case FieldType::Password:  return "password";
    case FieldType::Email:     return "email";
    case FieldType::Text:      return "text";  
    case FieldType::BigInt:    return "bigint";
    case FieldType::SmallInt:  return "smallint";
    case FieldType::Decimal:   return "decimal";
    case FieldType::Json:      return "json";
    case FieldType::Binary:    return "binary";
    case FieldType::Native:    return "native";

    default:                   return "unknown"; // sécurité
    }
}

// ─────────────────────────────────────────────────────────────
// Conversion string → enum
// Utilisé par :
// - parser YAML
// - import JSON (plus tard)
// ─────────────────────────────────────────────────────────────

inline std::optional<FieldType> field_type_from_string(std::string_view s) noexcept {
    // Normalisation en minuscules pour éviter les erreurs YAML
    // ex: "String", "STRING", "string" → "string"
    std::string lower{s};
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });

    if (lower == "string")    return FieldType::String;
    if (lower == "int")       return FieldType::Int;
    if (lower == "float")     return FieldType::Float;
    if (lower == "bool")      return FieldType::Bool;
    if (lower == "timestamp") return FieldType::Timestamp;
    if (lower == "uuid")      return FieldType::UUID;
    if (lower == "password")  return FieldType::Password;
    if (lower == "email")     return FieldType::Email;
    if (lower == "text")      return FieldType::Text;
    if (lower == "bigint")    return FieldType::BigInt;
    if (lower == "smallint")  return FieldType::SmallInt;
    if (lower == "decimal")   return FieldType::Decimal;
    if (lower == "json")      return FieldType::Json;
    if (lower == "binary")    return FieldType::Binary;


    // Type inconnu → parser devra gérer l'erreur
    return std::nullopt;
}

// ─────────────────────────────────────────────────────────────
// Helpers métier (très utiles pour runtime et codegen)
// ─────────────────────────────────────────────────────────────

// Indique si le type contient une logique métier supplémentaire
// (validation ou transformation)
constexpr bool is_logical_type(FieldType t) noexcept {
    return t == FieldType::Password || t == FieldType::Email || t == FieldType::Native;
}

// Indique si le type est numérique
constexpr bool is_numeric(FieldType t) noexcept {

    switch (t) {
    case FieldType::SmallInt:
    case FieldType::Int:
    case FieldType::BigInt:
    case FieldType::Float:
    case FieldType::Decimal:
        return true;

    default:
        return false;
    }
}

// Indique si le type est booléen
constexpr bool is_boolean(FieldType t) noexcept {
    return t == FieldType::Bool;
}

} // namespace sea::domain