#pragma once

// ─────────────────────────────────────────────────────────────
// SeaFilesTable
//
// Helper statique qui produit la DDL de la table système
// `sea_files` et expose les constantes liées (nom, colonnes).
//
// Pourquoi un fichier dédié ?
//   - sea_files n'est pas une entité métier déclarée en YAML :
//     elle est implicite et créée automatiquement par le
//     bootstrapper avant toutes les tables d'entités.
//   - Centraliser la définition évite la dérive (le runtime,
//     les handlers, et le bootstrapper doivent tous se
//     référer aux mêmes noms de colonnes).
//   - Découpler du mysql_schema_generator qui, lui, génère la
//     DDL des ENTITÉS du YAML (sea_files n'en est pas une).
//
// Schéma logique (cf. file_metadata.h) :
//
//   CREATE TABLE sea_files (
//     id              BINARY(16)   PRIMARY KEY,
//     original_name   VARCHAR(255) NOT NULL,
//     mime_type       VARCHAR(100) NOT NULL,
//     size_bytes      BIGINT       NOT NULL,
//     storage_path    VARCHAR(500) NOT NULL,
//     reference_count INT          NOT NULL DEFAULT 0,
//     created_at      TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP
//   ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
// ─────────────────────────────────────────────────────────────

#include <string>
#include <string_view>

namespace sea::infrastructure::persistence::mysql {

class SeaFilesTable {
public:
    // Nom de la table système. Préfixé "sea_" pour éviter toute
    // collision avec une entité utilisateur "files".
    static constexpr std::string_view TABLE_NAME = "sea_files";

    // Noms de colonnes — exposés en constantes pour que les
    // services consommateurs (FileService, repositories) ne
    // dupliquent pas les strings.
    static constexpr std::string_view COL_ID              = "id";
    static constexpr std::string_view COL_ORIGINAL_NAME   = "original_name";
    static constexpr std::string_view COL_MIME_TYPE       = "mime_type";
    static constexpr std::string_view COL_SIZE_BYTES      = "size_bytes";
    static constexpr std::string_view COL_STORAGE_PATH    = "storage_path";
    static constexpr std::string_view COL_REFERENCE_COUNT = "reference_count";
    static constexpr std::string_view COL_CREATED_AT      = "created_at";

    // Génère "CREATE TABLE IF NOT EXISTS sea_files (...)".
    //
    // Idempotent (IF NOT EXISTS) : peut être ré-exécutée à chaque
    // boot sans risque. La table existante n'est PAS modifiée
    // (même si on a ajouté une colonne dans le code) — cela
    // suivra le pattern de migration existant si on évolue le
    // schéma plus tard.
    [[nodiscard]] static std::string generate_create_table_sql();
};

} // namespace sea::infrastructure::persistence::mysql