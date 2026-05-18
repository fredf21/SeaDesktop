#pragma once

// ─────────────────────────────────────────────────────────────
// IFileStorage
//
// Interface abstraite pour les backends de stockage de fichiers.
// Volontairement SYNCHRONE : les consommateurs (handlers HTTP)
// wrapent les appels via IBlockingExecutor::submit() pour ne pas
// bloquer le reactor Seastar.
//
// Justifications de ce choix :
//   - Cohérent avec MysqlGenericRepository (sync, wrap au handler)
//   - Tests unitaires triviaux (pas besoin de reactor Seastar)
//   - Permet S3 future (SDK AWS C++ est sync de toute façon)
//   - Sépare le métier I/O de l'orchestration async
//
// Le contenu binaire est passé/retourné en std::string pour
// pragmatisme (binary-safe en C++17+, et c'est le type natif
// des body HTTP Seastar et des colonnes BLOB MySQL).
// ─────────────────────────────────────────────────────────────


#include <cstddef>
#include <memory>
#include <string>

namespace sea::infrastructure::storage {

class IFileStorage {
public:
    virtual ~IFileStorage() = default;

    // ─────────────────────────────────────────────────────────
    // Persiste un nouveau fichier dans le storage.
    //
    // Paramètres :
    //   relative_path : chemin RELATIF à la racine du storage.
    //                   Convention :
    //                   "<storage_path_du_champ>/<uuid>.<ext>"
    //                   ex: "users/avatars/550e8400-...png"
    //   content       : bytes bruts du fichier.
    //
    // Comportement :
    //   - Crée les sous-dossiers manquants (mkdir -p).
    //   - Applique storage_config.file_mode aux fichiers créés.
    //   - Applique storage_config.directory_mode aux dossiers.
    //   - ÉCRASE silencieusement si le path existe déjà
    //     (responsabilité de l'appelant de générer des UUIDs uniques).
    //
    // Sécurité :
    //   - Refuse les paths absolus
    //   - Refuse tout path qui canonicalisé sortirait du root
    //     (path traversal "../")
    //
    // Lève : sea_errors_handling::StorageException en cas d'échec
    //        (permission denied, disque plein, path invalide).
    // ─────────────────────────────────────────────────────────
    virtual void store(const std::string& relative_path,
                       const std::string& content) = 0;

    // ─────────────────────────────────────────────────────────
    // Lit un fichier depuis le storage.
    //
    // Paramètre :
    //   relative_path : même convention que store().
    //
    // Retour : contenu binaire du fichier.
    //
    // Lève : StorageException si fichier introuvable, accès
    //        refusé, ou path invalide.
    // ─────────────────────────────────────────────────────────
    [[nodiscard]] virtual std::string
    retrieve(const std::string& relative_path) = 0;

    // ─────────────────────────────────────────────────────────
    // Supprime un fichier du storage.
    //
    // Idempotent : ne lève PAS d'exception si le fichier n'existe
    // pas (consistent avec la sémantique HTTP DELETE 204/404 vs
    // 200). Retourne false dans ce cas.
    //
    // Lève : StorageException seulement en cas d'échec inattendu
    //        (permission, I/O), pas pour fichier inexistant.
    //
    // Retour : true si un fichier a été effectivement supprimé,
    //         false s'il n'existait pas.
    // ─────────────────────────────────────────────────────────
    virtual bool remove(const std::string& relative_path) = 0;

    // ─────────────────────────────────────────────────────────
    // Vérifie l'existence d'un fichier.
    //
    // Pure check, ne lit aucun byte. Utile pour valider les
    // référence_count avant suppression cascade.
    //
    // Retour : true si le fichier existe et est lisible.
    // ─────────────────────────────────────────────────────────
    [[nodiscard]] virtual bool
    exists(const std::string& relative_path) = 0;

    // ─────────────────────────────────────────────────────────
    // Retourne la taille d'un fichier en bytes.
    //
    // Utile pour cross-check du size_bytes stocké dans sea_files
    // vs la réalité du disque (détection de corruption).
    //
    // Lève : StorageException si fichier introuvable ou erreur I/O.
    // ─────────────────────────────────────────────────────────
    [[nodiscard]] virtual std::size_t
    size(const std::string& relative_path) = 0;
};

// Type d'usage standard : on partage l'instance via shared_ptr
// (cohérent avec IGenericRepository, IBlockingExecutor du projet).
using IFileStoragePtr = std::shared_ptr<IFileStorage>;

} // namespace sea::infrastructure::storage