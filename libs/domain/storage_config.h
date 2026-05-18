#pragma once

// ─────────────────────────────────────────────────────────────
// StorageConfig
//
// Configuration runtime du backend de stockage de fichiers.
// Cette config est lue depuis le YAML (Étape 6+) ou construite
// programmatiquement, puis passée à l'implémentation de
// IFileStorage à sa construction.
//
// Note de design : cette structure vit dans le domaine
// (sea::domain) parce qu'elle exprime une intention métier
// ("où range-t-on les fichiers de cette installation ?"),
// pas un détail d'implémentation. Les impls concrètes la
// consomment (FilesystemStorage, plus tard S3Storage...).
// ─────────────────────────────────────────────────────────────

#include <cstdint>
#include <string>

namespace sea::domain {

// Type de backend de stockage.
// Pour l'instant, seul Filesystem est implémenté. L'enum prépare
// l'extensibilité — c'est aussi ce qui justifie l'abstraction
// IFileStorage dès maintenant.
enum class StorageBackend {
    Filesystem,
    // S3,            // future
    // GoogleCloud,   // future
    // AzureBlob      // future
};

struct StorageConfig {
    // Backend à utiliser. Default : filesystem local.
    StorageBackend backend = StorageBackend::Filesystem;

    // Racine du storage pour le backend Filesystem.
    // Tous les storage_path déclarés au niveau des champs File
    // sont relatifs à cette racine. Convention recommandée :
    // un dossier absolu hors du dépôt source (ex: "/var/lib/seadesktop/uploads"
    // en prod, "./uploads" en dev).
    //
    // Le FilesystemStorage canonicalise ce chemin à l'init et
    // refuse toute écriture/lecture qui tenterait d'en sortir.
    std::string root_path;

    // Permissions Unix appliquées aux fichiers créés (mode octal).
    // 0640 = rw- r-- --- : owner read/write, group read, others nothing.
    // Sensé par défaut pour des fichiers uploadés via une app web.
    std::uint32_t file_mode = 0640;

    // Permissions Unix appliquées aux dossiers créés.
    // 0750 = rwx r-x --- : owner full, group read/exec, others nothing.
    std::uint32_t directory_mode = 0750;

    // ── helpers ─────────────────────────────────────────────

    [[nodiscard]] bool is_filesystem() const noexcept {
        return backend == StorageBackend::Filesystem;
    }

    [[nodiscard]] bool has_root_path() const noexcept {
        return !root_path.empty();
    }
};

} // namespace sea::domain