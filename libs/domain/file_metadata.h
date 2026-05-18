#pragma once

// ─────────────────────────────────────────────────────────────
// FileMetadata
//
// Représentation domaine d'un record de la table système sea_files.
// Cette structure est manipulée par :
//   - le FileService (application layer) pour orchestrer storage + DB
//   - les handlers HTTP (download : récupère metadata pour fixer
//     Content-Type et Content-Disposition)
//   - le bootstrapper (création/migration de la table sea_files)
//
// La table sea_files a le schéma logique suivant (DDL généré à
// l'Étape 5 par MysqlSchemaGenerator) :
//
//   CREATE TABLE sea_files (
//     id              BINARY(16)   PRIMARY KEY,    -- UUID v4
//     original_name   VARCHAR(255) NOT NULL,        -- nom client
//     mime_type       VARCHAR(100) NOT NULL,        -- ex: image/png
//     size_bytes      BIGINT       NOT NULL,
//     storage_path    VARCHAR(500) NOT NULL,        -- chemin RELATIF
//                                                   -- (la racine vient
//                                                   -- de StorageConfig)
//     reference_count INT          NOT NULL DEFAULT 0,
//     created_at      TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP
//   );
//
// Le storage_path stocké est toujours relatif à la racine du
// IFileStorage. Cela permet de migrer/déplacer la racine sans
// avoir à mettre à jour tous les records de sea_files.
// ─────────────────────────────────────────────────────────────

#include <chrono>
#include <cstdint>
#include <string>

namespace sea::domain {

struct FileMetadata {
    // UUID v4 sous forme canonique (36 chars avec dashes).
    // Stocké en BINARY(16) côté MySQL (via UUID_TO_BIN / BIN_TO_UUID).
    std::string id;

    // Nom du fichier tel qu'envoyé par le client.
    // Réutilisé dans le header Content-Disposition lors du download.
    // NE PAS utiliser comme nom de fichier sur disque (risque
    // path-traversal, collisions, caractères invalides).
    std::string original_name;

    // Type MIME détecté ou fourni par le client (ex: "image/png").
    // Réutilisé dans le header Content-Type lors du download.
    std::string mime_type;

    // Taille du fichier en bytes.
    std::size_t size_bytes = 0;

    // Chemin RELATIF dans le storage (ex: "users/avatars/abc-123.bin").
    // Ne contient PAS la racine — celle-ci est résolue à l'usage par
    // l'implémentation de IFileStorage.
    std::string storage_path;

    // Nombre d'entités qui référencent ce fichier.
    // Incrémenté à chaque INSERT/UPDATE pointant vers ce file_id,
    // décrémenté à chaque DELETE/UPDATE qui le déréférence.
    // Quand reference_count == 0 ET on_delete == Cascade, le fichier
    // est supprimé physiquement.
    std::int32_t reference_count = 0;

    // Horodatage de création (UTC).
    // Stocké en TIMESTAMP côté MySQL.
    std::chrono::system_clock::time_point created_at{};

    // ── helpers ─────────────────────────────────────────────

    [[nodiscard]] bool is_orphan() const noexcept {
        return reference_count <= 0;
    }
};

} // namespace sea::domain