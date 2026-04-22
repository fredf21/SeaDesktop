#pragma once
#include "runtime/dynamic_record.h"
#include "runtime/schema_runtime_registry.h"
#include <string>

namespace sea::infrastructure::persistence::utilities {

// ============================================================
// ERRORS
// ============================================================

/// \brief Codes d'erreurs utilisés dans la couche de persistance.
enum class ErrorCode{
    // Schema
    SchemaEmpty,
    EntityNotFound,
    FieldNotFound,
    RecordKeyMissing,        ///< Clé absente dans le record

    // Validation
    InvalidIdentifier,
    InvalidType,
    MissingRequiredField,
    InvalidEmail,
    ValueOutOfRange,

    // SQL
    InvalidSqlIdentifier,
    QueryBuildError,

    // Database
    ConnectionFailed,
    QueryFailed,

    // Generic
    UnknownError

};

/// \brief Représente une erreur structurée.
struct Error {
    ErrorCode code;
    std::string message;
};

/// \brief Conteneur d'erreurs accumulées.
struct ValidationResult {
    std::vector<Error> errors;

    /// \brief Indique si aucune erreur n'a été enregistrée.
    [[nodiscard]] bool ok() const noexcept {
        return errors.empty();
    }

    /// \brief Ajoute une erreur.
    void add(ErrorCode code, std::string msg) {
        errors.push_back({code, std::move(msg)});
    }
};


// ============================================================
// IDENTIFIER UTILS
// ============================================================

/// \brief Vérifie qu'un identifiant SQL est valide.
///
/// Règles :
/// - non vide
/// - uniquement [a-zA-Z0-9_]
inline bool validate_sql_identifier(const std::string& id, ValidationResult& validation_result) {
    validation_result.errors.clear();
    if (id.empty()) {
        validation_result.add(ErrorCode::InvalidSqlIdentifier,
                   "Identifiant SQL vide");
        return false;
    }

    for (char c : id) {
        if (!(std::isalnum(c) || c == '_')) {

            validation_result.add(ErrorCode::InvalidSqlIdentifier, "Identifiant SQL invalide: " + id);
            return false;
        }
    }
    return true;
}


// ============================================================
// ENTITY UTILS
// ============================================================

/// \brief Récupère une entité depuis le registre.
inline const sea::domain::Entity* get_required_entity(
    const runtime::SchemaRuntimeRegistry& registry,
    const std::string& entity_name,
    ValidationResult& validation_result) {

    const auto* entity = registry.find_entity(entity_name);

    if (entity == nullptr) {
        validation_result.add(ErrorCode::EntityNotFound,
                   "Entité introuvable: " + entity_name);
        return nullptr;
    }

    return entity;
}

/// \brief Résout le nom réel de la table SQL.
inline std::string resolve_table_name(const sea::domain::Entity& entity) {
    return entity.table_name.empty() ? entity.name : entity.table_name;
}


// ============================================================
// RECORD UTILS
// ============================================================

/// \brief Vérifie que toutes les clés existent dans le schéma.
inline bool validate_record_keys(
    const runtime::SchemaRuntimeRegistry& registry,
    const std::string& entity_name,
    const runtime::DynamicRecord& record,
    ValidationResult& validation_result)
{
    validation_result.errors.clear();
    for (const auto& [key, _] : record) {
        if (registry.find_field(entity_name, key) == nullptr) {
            validation_result.add(ErrorCode::FieldNotFound, "Champ ou entite non trouvee");
            return false;
        }
        if(!validate_sql_identifier(key, validation_result))
            return false;
    }
    return true;
}

/// \brief Récupère une valeur obligatoire dans un record.
inline const runtime::DynamicValue* get_required_value(
    const runtime::DynamicRecord& record,
    const std::string& key, ValidationResult& validation_result)
{
    validation_result.errors.clear();
    auto it = record.find(key);
    if (it == record.end()) {
        validation_result.add(ErrorCode::MissingRequiredField, "Champ manquant: " + key);
        return nullptr;
    }
    return &it->second;
}


// ============================================================
// COLUMN ORDER UTILS
// ============================================================

/// \brief Retourne les colonnes présentes dans le record
/// dans l'ordre du schéma.
///
/// Évite les problèmes liés à unordered_map.
inline std::vector<std::string> collect_columns_in_schema_order(
    const sea::domain::Entity& entity,
    const runtime::DynamicRecord& record,
    bool exclude_id = false) {

    std::vector<std::string> columns;

    for (const auto& field : entity.fields) {
        if (exclude_id && field.name == "id") {
            continue;
        }

        if (record.find(field.name) != record.end()) {
            columns.push_back(field.name);
        }
    }

    return columns;
}
}
