#pragma once

#include <string>
#include <string_view>

namespace sea::domain {

// ─────────────────────────────────────────────────────────────
// Type de relation entre deux entités
// ─────────────────────────────────────────────────────────────
enum class RelationKind {
    BelongsTo,   // clé étrangère dans cette entité
    // ex: Post appartient à User -> post.user_id

    HasMany,     // clé étrangère dans l'entité cible
    // ex: User a plusieurs Posts -> post.user_id

    HasOne,      // comme HasMany mais cardinalité 1
    // ex: User a un Profile -> profile.user_id

    ManyToMany,  // table pivot générée automatiquement
    // ex: User <-> Role -> user_roles
};

// ─────────────────────────────────────────────────────────────
// Politique de suppression SQL sur clé étrangère
// ─────────────────────────────────────────────────────────────
enum class OnDelete {
    Cascade,   // supprimer les éléments liés
    SetNull,   // mettre la FK à NULL
    Restrict,  // empêcher la suppression
};

// Conversion enum -> string
constexpr std::string_view to_string(RelationKind kind) noexcept {
    switch (kind) {
    case RelationKind::BelongsTo: return "belongs_to";
    case RelationKind::HasMany:   return "has_many";
    case RelationKind::HasOne:    return "has_one";
    case RelationKind::ManyToMany:return "many_to_many";
    default:                      return "unknown";
    }
}

constexpr std::string_view to_string(OnDelete rule) noexcept {
    switch (rule) {
    case OnDelete::Cascade:  return "cascade";
    case OnDelete::SetNull:  return "set_null";
    case OnDelete::Restrict: return "restrict";
    default:                 return "unknown";
    }
}

// ─────────────────────────────────────────────────────────────
// Relation entre deux entités métier
// Exemple :
//   Post belongsTo User
//   User hasMany Post
//   User manyToMany Role
// ─────────────────────────────────────────────────────────────
struct Relation {
    std::string  name;           // ex: "author", "posts", "roles"
    std::string  target_entity;  // ex: "User", "Post", "Role"

    RelationKind kind;
    OnDelete     on_delete = OnDelete::Restrict;

    // Nom de la clé étrangère
    // Utilisé surtout pour BelongsTo / HasOne / HasMany
    // ex: "user_id"
    std::string fk_column;

    // Nom de la table pivot
    // Utilisé seulement pour ManyToMany
    // ex: "user_roles"
    std::string pivot_table;

    // ── helpers ─────────────────────────────────────────────

    [[nodiscard]] bool uses_local_foreign_key() const noexcept {
        return kind == RelationKind::BelongsTo;
    }

    [[nodiscard]] bool uses_target_foreign_key() const noexcept {
        return kind == RelationKind::HasMany || kind == RelationKind::HasOne;
    }

    [[nodiscard]] bool uses_pivot_table() const noexcept {
        return kind == RelationKind::ManyToMany;
    }

    [[nodiscard]] bool is_to_many() const noexcept {
        return kind == RelationKind::HasMany || kind == RelationKind::ManyToMany;
    }
};

} // namespace sea::domain