#pragma once

#include "../field_type.h"

#include <string_view>

// ─────────────────────────────────────────────────────────────
// Mapping FieldType → MySQL types
//
// Utilise par MysqlSchemaGenerator pour produire le SQL
// CREATE TABLE / ADD COLUMN lors des migrations automatiques.
// ─────────────────────────────────────────────────────────────

namespace sea::domain {

// ─────────────────────────────────────────────────────────────
// Mapping basique (sans tenir compte des contraintes du field).
//
// Pour un mapping precis qui consulte field.max_length, voir
// to_mysql_column_type(const Field&) dans le MysqlSchemaGenerator.
// ─────────────────────────────────────────────────────────────
constexpr std::string_view to_mysql_type(FieldType t) noexcept {
    switch (t) {
    case FieldType::String:    return "VARCHAR(255)";
    case FieldType::Text:      return "TEXT";
    case FieldType::Int:       return "BIGINT";
    case FieldType::Float:     return "DOUBLE";
    case FieldType::Bool:      return "BOOLEAN";
    case FieldType::Timestamp: return "TIMESTAMP";
    case FieldType::UUID:      return "BINARY(16)";       // Pour UUID_TO_BIN/BIN_TO_UUID

    // Types métier stockés comme VARCHAR
    case FieldType::Password:  return "VARCHAR(255)";
    case FieldType::Email:     return "VARCHAR(255)";

    // ─────────────────────────────────────────────────────────
    // File : la colonne stocke un UUID v4 référençant sea_files.id
    //
    // Le BINARY(16) est identique au mapping UUID — on partage la
    // même représentation et donc les mêmes helpers SQL
    // (UUID_TO_BIN / BIN_TO_UUID) pour la sérialisation côté
    // repository.
    //
    // La FK vers sea_files.id est ajoutée séparément dans
    // generate_create_table_sql (avec l'ON DELETE selon le
    // FileFieldConfig::on_delete).
    // ─────────────────────────────────────────────────────────
    case FieldType::File:      return "BINARY(16)";

    default:                   return "VARCHAR(255)";
    }
}

// ─────────────────────────────────────────────────────────────
// Helpers MySQL pour la generation de schema
// ─────────────────────────────────────────────────────────────

// Indique si un type MySQL accepte AUTO_INCREMENT (entiers seulement)
constexpr bool mysql_supports_auto_increment(FieldType t) noexcept {
    return t == FieldType::Int;
}

// Indique si un type est stocke en BINARY (UUID en MySQL)
// → necessite UUID_TO_BIN / BIN_TO_UUID dans les queries
//
// File est aussi en BINARY(16) car la colonne référence sea_files.id
// (qui est lui-même un UUID).
constexpr bool mysql_uses_binary_storage(FieldType t) noexcept {
    return t == FieldType::UUID || t == FieldType::File;
}

} // namespace sea::domain