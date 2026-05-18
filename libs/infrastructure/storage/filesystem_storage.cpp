#include "filesystem_storage.h"
#include "exception_handling.h"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <system_error>

#include <sys/stat.h>     // chmod (POSIX)

namespace sea::infrastructure::storage {

namespace fs = std::filesystem;

namespace {

// Lève une StorageException avec un message uniforme préfixé par
// la valeur de errno (pour les opérations POSIX) ou par le message
// std::error_code.
[[noreturn]] void throw_io_error(const std::string& operation,
                                 const std::string& path,
                                 const std::error_code& ec) {
    throw sea::sea_errors_handling::StorageException(
        "[FilesystemStorage] " + operation + " a echoue sur '" + path +
        "': " + ec.message() + " (code=" + std::to_string(ec.value()) + ").");
}

[[noreturn]] void throw_sandbox_violation(const std::string& relative_path,
                                          const std::string& reason) {
    throw sea::sea_errors_handling::StorageException(
        "[FilesystemStorage] Acces refuse pour '" + relative_path +
        "' : " + reason + ".");
}

// Applique chmod sur un path (best-effort : on log mais on n'échoue
// pas si la syscall retourne -1, car le fichier a été créé avec
// succès — c'est juste les permissions qui n'ont pas pu être ajustées).
void apply_mode(const fs::path& p, std::uint32_t mode) {
    ::chmod(p.c_str(), static_cast<mode_t>(mode));
}

} // namespace

// ─────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────
FilesystemStorage::FilesystemStorage(sea::domain::StorageConfig cfg)
    : config_(std::move(cfg))
{
    if (!config_.is_filesystem()) {
        throw sea::sea_errors_handling::StorageException(
            "[FilesystemStorage] StorageConfig::backend != Filesystem.");
    }

    if (!config_.has_root_path()) {
        throw sea::sea_errors_handling::StorageException(
            "[FilesystemStorage] StorageConfig::root_path est vide.");
    }

    // Crée le dossier racine s'il n'existe pas.
    // create_directories est idempotent et crée toute la hiérarchie
    // parente si nécessaire.
    std::error_code ec;
    fs::create_directories(config_.root_path, ec);
    if (ec) {
        throw_io_error("création de la racine", config_.root_path, ec);
    }

    // Vérifie que la racine est bien un dossier (pas un fichier
    // qui existait déjà).
    if (!fs::is_directory(config_.root_path, ec)) {
        throw sea::sea_errors_handling::StorageException(
            "[FilesystemStorage] root_path '" + config_.root_path +
            "' n'est pas un dossier.");
    }

    // Applique les permissions sur la racine (best-effort).
    apply_mode(config_.root_path, config_.directory_mode);

    // Canonicalise la racine (résout '..', '.', symlinks).
    // C'est cette version qui sert de référence à toutes les
    // vérifications de sandbox.
    root_canonical_ = fs::canonical(config_.root_path, ec);
    if (ec) {
        throw_io_error("canonicalisation de la racine",
                       config_.root_path, ec);
    }
}

// ─────────────────────────────────────────────────────────────
// resolve_safe_path : coeur du sandboxing
// ─────────────────────────────────────────────────────────────
fs::path FilesystemStorage::resolve_safe_path(
    const std::string& relative_path,
    bool must_exist) const
{
    if (relative_path.empty()) {
        throw_sandbox_violation(relative_path, "chemin vide");
    }

    // Refuse explicitement les paths absolus (premier rempart).
    const fs::path raw(relative_path);
    if (raw.is_absolute()) {
        throw_sandbox_violation(relative_path, "chemin absolu interdit");
    }

    // Construit le chemin candidat : root/relative.
    fs::path candidate = root_canonical_ / raw;

    // Résolution lexicale : normalise '..' et '.' SANS toucher au
    // filesystem. Pas de canonical() ici car le fichier final
    // n'existe pas encore pour une écriture.
    //
    // ex: root="/var/lib/sea/uploads", raw="users/../../etc/passwd"
    //  -> candidate = "/var/lib/sea/uploads/users/../../etc/passwd"
    //  -> lexically_normal = "/var/lib/sea/etc/passwd"
    //  -> commence-t-il par root ? NON -> rejeté.
    candidate = candidate.lexically_normal();

    // Vérification du sandbox : le chemin résolu doit commencer par
    // la racine canonicalisée. C'est ce check qui bloque les
    // path-traversal résiduels qui auraient survécu à la validation
    // schema_validator (filet de sécurité runtime).
    {
        const auto root_str = root_canonical_.string();
        const auto cand_str = candidate.string();

        if (cand_str.size() < root_str.size() ||
            cand_str.compare(0, root_str.size(), root_str) != 0) {
            throw_sandbox_violation(relative_path,
                                    "chemin resolu sort du sandbox");
        }

        // Évite "/root/foo_evil" qui matcherait "/root/foo" par prefix
        // sans être réellement dedans. Le caractère suivant doit être
        // un séparateur OU la fin de la chaîne.
        if (cand_str.size() > root_str.size()) {
            if (cand_str[root_str.size()] != '/') {
                throw_sandbox_violation(relative_path,
                                        "chemin resolu n'est pas un sous-chemin direct du sandbox");
            }
        }
    }

    // Si le fichier doit déjà exister (lecture/delete/etc.), on peut
    // canonicaliser le chemin complet pour suivre les symlinks et
    // vérifier qu'ils ne pointent pas hors-sandbox.
    if (must_exist) {
        std::error_code ec;
        fs::path resolved = fs::canonical(candidate, ec);
        if (ec) {
            // Le chemin n'existe pas — ce n'est pas une violation,
            // c'est juste un fichier introuvable. On retourne le
            // chemin non-résolu pour que l'appelant lève la bonne
            // exception (fichier non trouvé vs accès refusé).
            return candidate;
        }

        // Re-vérifie le sandbox après résolution des symlinks.
        const auto root_str = root_canonical_.string();
        const auto resolved_str = resolved.string();
        if (resolved_str.size() < root_str.size() ||
            resolved_str.compare(0, root_str.size(), root_str) != 0) {
            throw_sandbox_violation(relative_path,
                                    "symlink pointant hors du sandbox");
        }
        return resolved;
    }

    return candidate;
}

// ─────────────────────────────────────────────────────────────
// store
// ─────────────────────────────────────────────────────────────
void FilesystemStorage::store(const std::string& relative_path,
                              const std::string& content)
{
    const fs::path target = resolve_safe_path(relative_path, /*must_exist=*/false);

    // Crée les sous-dossiers parents.
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    if (ec) {
        throw_io_error("création des dossiers parents", relative_path, ec);
    }

    // Applique le mode aux dossiers créés (best-effort, descend
    // depuis la racine jusqu'au parent direct).
    {
        fs::path walk = root_canonical_;
        for (const auto& part : target.parent_path().lexically_relative(root_canonical_)) {
            walk /= part;
            apply_mode(walk, config_.directory_mode);
        }
    }

    // Écriture binaire. std::ofstream en mode binary est suffisant ;
    // on garde la simplicité du MVP (pas de write atomique via
    // rename, pas de fsync). À reconsidérer si on observe des
    // corruptions sur crash.
    std::ofstream ofs(target, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        throw sea::sea_errors_handling::StorageException(
            "[FilesystemStorage] Impossible d'ouvrir '" + relative_path +
            "' en ecriture.");
    }

    ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!ofs) {
        throw sea::sea_errors_handling::StorageException(
            "[FilesystemStorage] Echec de l'ecriture de '" + relative_path +
            "' (disque plein ? quota ?).");
    }
    ofs.close();

    apply_mode(target, config_.file_mode);
}

// ─────────────────────────────────────────────────────────────
// retrieve
// ─────────────────────────────────────────────────────────────
std::string FilesystemStorage::retrieve(const std::string& relative_path)
{
    const fs::path target = resolve_safe_path(relative_path, /*must_exist=*/true);

    std::error_code ec;
    if (!fs::exists(target, ec)) {
        throw sea::sea_errors_handling::StorageException(
            "[FilesystemStorage] Fichier introuvable: '" + relative_path + "'.");
    }
    if (!fs::is_regular_file(target, ec)) {
        throw sea::sea_errors_handling::StorageException(
            "[FilesystemStorage] '" + relative_path +
            "' n'est pas un fichier regulier.");
    }

    std::ifstream ifs(target, std::ios::binary);
    if (!ifs) {
        throw sea::sea_errors_handling::StorageException(
            "[FilesystemStorage] Impossible d'ouvrir '" + relative_path +
            "' en lecture.");
    }

    // Lecture en une fois. Pour les très gros fichiers, on
    // streamera plus tard (cf. Étape 8 quand on intègre les
    // routes de download — Seastar permet de chunker côté HTTP).
    const auto file_size = fs::file_size(target, ec);
    if (ec) {
        throw_io_error("file_size", relative_path, ec);
    }

    std::string content;
    content.resize(file_size);
    ifs.read(content.data(), static_cast<std::streamsize>(file_size));
    if (!ifs && !ifs.eof()) {
        throw sea::sea_errors_handling::StorageException(
            "[FilesystemStorage] Echec de lecture de '" + relative_path + "'.");
    }

    return content;
}

// ─────────────────────────────────────────────────────────────
// remove (idempotent)
// ─────────────────────────────────────────────────────────────
bool FilesystemStorage::remove(const std::string& relative_path)
{
    const fs::path target = resolve_safe_path(relative_path, /*must_exist=*/true);

    std::error_code ec;
    if (!fs::exists(target, ec)) {
        return false;   // idempotent : fichier déjà absent
    }

    const bool removed = fs::remove(target, ec);
    if (ec) {
        throw_io_error("remove", relative_path, ec);
    }
    return removed;
}

// ─────────────────────────────────────────────────────────────
// exists
// ─────────────────────────────────────────────────────────────
bool FilesystemStorage::exists(const std::string& relative_path)
{
    // resolve_safe_path peut throw (path invalide) — on laisse
    // remonter : un appel exists() avec un path malformé est
    // bien une erreur d'utilisation.
    const fs::path target = resolve_safe_path(relative_path, /*must_exist=*/true);

    std::error_code ec;
    return fs::exists(target, ec) && fs::is_regular_file(target, ec);
}

// ─────────────────────────────────────────────────────────────
// size
// ─────────────────────────────────────────────────────────────
std::size_t FilesystemStorage::size(const std::string& relative_path)
{
    const fs::path target = resolve_safe_path(relative_path, /*must_exist=*/true);

    std::error_code ec;
    if (!fs::exists(target, ec)) {
        throw sea::sea_errors_handling::StorageException(
            "[FilesystemStorage] Fichier introuvable: '" + relative_path + "'.");
    }

    const auto sz = fs::file_size(target, ec);
    if (ec) {
        throw_io_error("file_size", relative_path, ec);
    }
    return static_cast<std::size_t>(sz);
}

} // namespace sea::infrastructure::storage