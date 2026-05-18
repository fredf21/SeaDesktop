#pragma once

// ─────────────────────────────────────────────────────────────
// Configuration déclarative d'un champ File
//
// Cette config est attachée à un Field lorsque field.type == FieldType::File.
// Elle est lue depuis le sous-bloc `file:` du YAML et pilote :
//   - la validation à l'upload (size, mime, extension)
//   - le sous-dossier de stockage (storage_path)
//   - le comportement lors de la suppression de l'entité parente (on_delete)
//
// Exemple YAML correspondant :
//
//   - name: avatar
//     type: file
//     file:
//       max_size: 5MB
//       allowed_mime_types: [image/png, image/jpeg]
//       allowed_extensions: [.png, .jpg]
//       storage_path: users/avatars
//       on_delete: cascade
// ─────────────────────────────────────────────────────────────

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sea::domain {

// ─────────────────────────────────────────────────────────────
// Comportement à appliquer au fichier référencé lorsque
// l'entité qui le détient est supprimée.
//
// Note : cet enum est distinct de sea::domain::OnDelete (utilisé
// par les relations FK classiques) pour deux raisons :
//   1. La sémantique est différente : ici on parle de la durée
//      de vie d'un fichier dans le storage + sea_files, pas
//      d'une contrainte FK SQL standard.
//   2. Les options pertinentes diffèrent (pas de NO_ACTION ici).
// ─────────────────────────────────────────────────────────────
enum class OnDeleteFile {
    // Décrémente le reference_count. Si 0, supprime le record
    // de sea_files ET le fichier physique du storage.
    Cascade,

    // Met le champ FK à NULL côté entité, garde le fichier.
    // Le reference_count est tout de même décrémenté.
    // Utile pour des fichiers partagés où l'on veut un GC offline.
    SetNull,

    // Empêche la suppression de l'entité tant qu'un fichier
    // est référencé. La requête DELETE renvoie une erreur 409.
    Restrict
};

// ─────────────────────────────────────────────────────────────
// Conversion enum ↔ string (utilisé par parser YAML et logs)
// ─────────────────────────────────────────────────────────────

constexpr std::string_view to_string(OnDeleteFile rule) noexcept {
    switch (rule) {
    case OnDeleteFile::Cascade:  return "cascade";
    case OnDeleteFile::SetNull:  return "set_null";
    case OnDeleteFile::Restrict: return "restrict";
    }
    return "unknown";
}

inline std::optional<OnDeleteFile> on_delete_file_from_string(std::string_view s) noexcept {
    std::string lower{s};
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });

    if (lower == "cascade")  return OnDeleteFile::Cascade;
    if (lower == "set_null") return OnDeleteFile::SetNull;
    if (lower == "restrict") return OnDeleteFile::Restrict;

    return std::nullopt;
}

// ─────────────────────────────────────────────────────────────
// FileFieldConfig
//
// Toutes les contraintes déclaratives appliquées à un champ File.
// Tous les vecteurs/optionnels vides signifient "pas de contrainte"
// (cf. helpers ci-dessous).
// ─────────────────────────────────────────────────────────────
struct FileFieldConfig {
    // Taille maximale autorisée pour le fichier upload, en bytes.
    // std::nullopt => pas de limite (le bootstrapper appliquera
    // toutefois la limite globale du serveur HTTP, cf. http_limit).
    std::optional<std::size_t> max_size_bytes;

    // Liste blanche de types MIME acceptés (ex: "image/png").
    // Vide => tous les types acceptés.
    std::vector<std::string> allowed_mime_types;

    // Liste blanche d'extensions acceptées (ex: ".png").
    // Les extensions sont normalisées en minuscules, avec ou sans
    // point initial (le parser ajoute le point manquant).
    // Vide => toutes les extensions acceptées.
    std::vector<std::string> allowed_extensions;

    // Sous-dossier relatif à la racine du storage où ce champ
    // stocke ses fichiers. Ex: "users/avatars".
    // Vide => racine du storage (déconseillé, source de collisions).
    // Toujours relatif (validé par schema_validator : pas de "../"
    // ni de chemin absolu).
    std::string storage_path;

    // Comportement à la suppression de l'entité parente.
    // Default : Cascade (intuitif pour la majorité des cas).
    OnDeleteFile on_delete = OnDeleteFile::Cascade;

    // ── helpers ─────────────────────────────────────────────

    [[nodiscard]] bool has_size_limit() const noexcept {
        return max_size_bytes.has_value();
    }

    [[nodiscard]] bool has_mime_filter() const noexcept {
        return !allowed_mime_types.empty();
    }

    [[nodiscard]] bool has_extension_filter() const noexcept {
        return !allowed_extensions.empty();
    }

    [[nodiscard]] bool has_storage_path() const noexcept {
        return !storage_path.empty();
    }

    // Vérifie qu'un MIME type fourni respecte le filtre.
    // Si le filtre est vide, accepte tout.
    [[nodiscard]] bool accepts_mime(std::string_view mime) const noexcept {
        if (allowed_mime_types.empty()) {
            return true;
        }

        for (const auto& allowed : allowed_mime_types) {
            if (allowed == mime) {
                return true;
            }
        }
        return false;
    }

    // Vérifie qu'une extension (avec ou sans point) respecte le filtre.
    // Si le filtre est vide, accepte tout.
    // Comparaison case-insensitive.
    [[nodiscard]] bool accepts_extension(std::string_view ext) const noexcept {
        if (allowed_extensions.empty()) {
            return true;
        }

        // Normalisation : ajout du point si manquant, lowercase
        std::string normalized;
        normalized.reserve(ext.size() + 1);
        if (!ext.empty() && ext.front() != '.') {
            normalized.push_back('.');
        }
        for (char c : ext) {
            normalized.push_back(
                static_cast<char>(std::tolower(static_cast<unsigned char>(c)))
                );
        }

        for (const auto& allowed : allowed_extensions) {
            if (allowed == normalized) {
                return true;
            }
        }
        return false;
    }

    // Vérifie qu'une taille (en bytes) respecte la limite.
    // Si pas de limite définie, accepte tout.
    [[nodiscard]] bool accepts_size(std::size_t size) const noexcept {
        if (!max_size_bytes.has_value()) {
            return true;
        }
        return size <= *max_size_bytes;
    }
};

} // namespace sea::domain