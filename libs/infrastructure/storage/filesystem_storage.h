#pragma once

// ─────────────────────────────────────────────────────────────
// FilesystemStorage
//
// Implémentation locale de IFileStorage basée sur std::filesystem.
// Stocke les fichiers dans un dossier racine configuré, avec
// sandboxing strict (refus de toute évasion via path traversal,
// path absolu, ou symlink pointant hors-sandbox).
//
// Cette implémentation est volontairement simple et bloquante :
//   - utilisée hors du reactor Seastar via IBlockingExecutor
//   - pas de cache, pas de pré-allocation, pas de streaming
//   - convient au MVP et aux installations small-to-medium
//
// Pour des volumes plus importants, on remplacera par S3Storage
// (la même interface IFileStorage permet la substitution sans
// toucher au code applicatif).
// ─────────────────────────────────────────────────────────────

#include "i_file_storage.h"
#include "storage_config.h"

#include <filesystem>
#include <string>

namespace sea::infrastructure::storage {

class FilesystemStorage final : public IFileStorage {
public:
    // Construction depuis une StorageConfig. La racine est
    // canonicalisée immédiatement (résolution des '..', symlinks,
    // chemins relatifs) et stockée pour comparaison rapide à
    // chaque opération.
    //
    // Lève StorageException si :
    //   - cfg.backend != Filesystem
    //   - cfg.root_path est vide
    //   - le dossier root n'existe pas et ne peut pas être créé
    //   - cfg.root_path résolu pointe vers autre chose qu'un
    //     dossier (ex: un fichier existant)
    explicit FilesystemStorage(sea::domain::StorageConfig cfg);

    // ── IFileStorage ────────────────────────────────────────

    void store(const std::string& relative_path,
               const std::string& content) override;

    [[nodiscard]] std::string
    retrieve(const std::string& relative_path) override;

    bool remove(const std::string& relative_path) override;

    [[nodiscard]] bool
    exists(const std::string& relative_path) override;

    [[nodiscard]] std::size_t
    size(const std::string& relative_path) override;

    // ── helpers publics (utiles pour tests et logs) ─────────

    // Retourne la racine canonicalisée. Read-only — exposée
    // pour debug et tests.
    [[nodiscard]] const std::filesystem::path&
    root() const noexcept { return root_canonical_; }

private:
    sea::domain::StorageConfig config_;

    // Version canonicalisée de config_.root_path. Stockée pour
    // éviter la canonicalisation à chaque opération et pour
    // servir de référence aux checks de sandbox.
    std::filesystem::path root_canonical_;

    // Résout un chemin relatif fourni par l'appelant en chemin
    // absolu sandboxé.
    //
    // Cette méthode est le COEUR de la sécurité : elle garantit
    // qu'aucune opération ne s'effectue en dehors de root_canonical_.
    //
    // Vérifications appliquées :
    //   1. relative_path n'est pas vide
    //   2. relative_path n'est pas absolu
    //   3. après concaténation + résolution lexicale ('..', '.'),
    //      le chemin résultant commence bien par root_canonical_
    //   4. (pour les lectures) si une partie du chemin existe et
    //      est un symlink, on vérifie qu'il ne pointe pas hors
    //      du sandbox
    //
    // Le boolean must_exist indique si le fichier doit déjà
    // exister (lecture/delete/exists/size) ou non (écriture).
    // Cela change la façon dont les symlinks sont résolus :
    // pour une écriture, le fichier final n'existe pas encore,
    // donc on ne peut pas utiliser canonical() dessus.
    //
    // Lève StorageException en cas de violation du sandbox.
    [[nodiscard]] std::filesystem::path
    resolve_safe_path(const std::string& relative_path,
                      bool must_exist) const;
};

} // namespace sea::infrastructure::storage