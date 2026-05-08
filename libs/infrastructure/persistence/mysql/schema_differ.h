#pragma once

#include "entity.h"
#include "field.h"
#include "mysql_introspector.h"

#include <string>
#include <vector>

namespace sea::infrastructure::persistence::mysql {

// ─────────────────────────────────────────────────────────────
// Type de changement detecte sur une colonne ou un index
// ─────────────────────────────────────────────────────────────
enum class ColumnDiffKind {
    Added,                  // Field absent en MySQL, present dans YAML  (Phase A)
    TypeChanged,            // Type MySQL ≠ type YAML                   (Phase B.1)
    NullabilityChanged,     // NULL/NOT NULL different                  (Phase B.1)
    DefaultChanged,         // DEFAULT different                        (Phase B.1)

    //indexes
    IndexAdded,             // Field.indexed=true, pas d'INDEX en MySQL
    IndexRemoved,           // INDEX en MySQL, Field.indexed=false
    UniqueAdded,            // Field.unique=true, pas de UNIQUE en MySQL
    UniqueRemoved,          // UNIQUE en MySQL, Field.unique=false
};

constexpr std::string_view to_string(ColumnDiffKind k) noexcept {
    switch (k) {
    case ColumnDiffKind::Added:               return "added";
    case ColumnDiffKind::TypeChanged:         return "type_changed";
    case ColumnDiffKind::NullabilityChanged:  return "nullability_changed";
    case ColumnDiffKind::DefaultChanged:      return "default_changed";
    case ColumnDiffKind::IndexAdded:          return "index_added";
    case ColumnDiffKind::IndexRemoved:        return "index_removed";
    case ColumnDiffKind::UniqueAdded:         return "unique_added";
    case ColumnDiffKind::UniqueRemoved:       return "unique_removed";
    }
    return "unknown";
}

// ─────────────────────────────────────────────────────────────
// Description d'un changement detecte
// ─────────────────────────────────────────────────────────────
struct ColumnDiff {
    ColumnDiffKind kind;
    std::string table_name;
    std::string column_name;

    // Pour TypeChanged
    std::string current_type;
    std::string target_type;

    // Pour Added : description du field a ajouter
    // Pour les autres : reference vers le field YAML cible
    const sea::domain::Field* target_field = nullptr;

    // Pour IndexRemoved/UniqueRemoved : nom de l'index a supprimer
    std::string index_name_to_drop;

    // Indique si le changement est "safe"
    bool is_safe = true;

    // Description humaine pour les logs
    std::string description;
};

// ─────────────────────────────────────────────────────────────
// SchemaDiffer
//
// Compare le schema YAML d'une entite avec l'etat actuel de MySQL
// et produit une liste de differences.
//
// Phase B.1 : Added + TypeChanged + NullabilityChanged + DefaultChanged
// Phase B.2 : IndexAdded + IndexRemoved + UniqueAdded + UniqueRemoved
// ─────────────────────────────────────────────────────────────
class SchemaDiffer {
public:
    // Compare une entity YAML avec une TableInfo MySQL pour les COLONNES.
    [[nodiscard]] static std::vector<ColumnDiff>
    compute_column_diffs(
        const sea::domain::Entity& entity,
        const TableInfo& table_info
        );

    // Compare les INDEXES (INDEX + UNIQUE).
    [[nodiscard]] static std::vector<ColumnDiff>
    compute_index_diffs(
        const sea::domain::Entity& entity,
        const TableInfo& table_info
        );

    // Indique si un changement de type est "safe"
    [[nodiscard]] static bool
    is_type_change_safe(
        const std::string& current_mysql_type,
        const sea::domain::Field& target_field
        );

private:
    [[nodiscard]] static std::string
    field_to_target_type(const sea::domain::Field& field);

    [[nodiscard]] static bool
    types_are_compatible(
        const std::string& current_mysql_type,
        const std::string& target_mysql_type
        );

    // helpers pour les indexes
    [[nodiscard]] static bool
    column_has_simple_index(
        const TableInfo& table_info,
        const std::string& column_name
        );

    [[nodiscard]] static bool
    column_has_unique_index(
        const TableInfo& table_info,
        const std::string& column_name
        );

    [[nodiscard]] static std::string
    find_index_name_for_column(
        const TableInfo& table_info,
        const std::string& column_name,
        bool look_for_unique
        );

    // Indique si une FK pointe vers une colonne (l'INDEX FK est auto-genere)
    [[nodiscard]] static bool
    column_is_fk(
        const sea::domain::Entity& entity,
        const std::string& column_name
        );
};

} // namespace sea::infrastructure::persistence::mysql