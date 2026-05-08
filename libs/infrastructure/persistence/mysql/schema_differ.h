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
    Added,
    TypeChanged,
    NullabilityChanged,
    DefaultChanged,
    IndexAdded,
    IndexRemoved,
    UniqueAdded,
    UniqueRemoved,
    Renamed,
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
    case ColumnDiffKind::Renamed:             return "renamed";
    }
    return "unknown";
}

// ─────────────────────────────────────────────────────────────
// Description d'un changement detecte
// ─────────────────────────────────────────────────────────────
struct ColumnDiff {
    ColumnDiffKind kind;
    std::string table_name;
    std::string column_name;        // nom NEW (ou name si non-rename)

    // Pour TypeChanged
    std::string current_type;
    std::string target_type;

    // Pour Added/Modified : reference vers le field YAML cible
    const sea::domain::Field* target_field = nullptr;

    // Pour IndexRemoved/UniqueRemoved : nom de l'index a supprimer
    std::string index_name_to_drop;

    // Pour Renamed : ancien nom de la colonne
    std::string previous_name;

    // Pour Renamed : score de confiance (100 = annotation explicite, < 100 = heuristique)
    int rename_confidence_score = 0;

    bool is_safe = true;
    std::string description;
};

// ─────────────────────────────────────────────────────────────
// SchemaDiffer
//
// Phase A   : Added
// Phase B.1 : TypeChanged + NullabilityChanged + DefaultChanged
// Phase B.2 : IndexAdded + IndexRemoved + UniqueAdded + UniqueRemoved
// Phase B.3 : Renamed (annotation explicite + heuristique)
// ─────────────────────────────────────────────────────────────
class SchemaDiffer {
public:
    // ── Phase A + B.1 ──
    [[nodiscard]] static std::vector<ColumnDiff>
    compute_column_diffs(
        const sea::domain::Entity& entity,
        const TableInfo& table_info
        );

    // ── Phase B.2 ──
    [[nodiscard]] static std::vector<ColumnDiff>
    compute_index_diffs(
        const sea::domain::Entity& entity,
        const TableInfo& table_info
        );

    // Detecte les renames de colonnes en 2 phases :
    //
    // Phase 1 (annotations explicites, score = 100) :
    //   Pour chaque field YAML avec previous_name :
    //     Si MySQL a la colonne previous_name ET pas le current_name
    //     → Rename explicite
    //
    // Phase 2 (heuristique automatique, score < 100) :
    //   Pour chaque colonne MySQL "orpheline" (pas dans le YAML
    //   et pas deja matchee par une annotation) :
    //     Cherche un field YAML "non-matche" avec score eleve :
    //       - meme type (+50)
    //       - position similaire (+30)
    //       - meme nullability (+10)
    //       - meme default (+5)
    //       - meme indexed/unique (+5)
    //     Si meilleur score >= 90 → rename heuristique
    //
    // Le bootstrapper decide d'appliquer ou non selon le mode :
    //   - score = 100 (explicite) : applique en mode modified+aggressive
    //   - score < 100 (heuristique) : applique uniquement en mode aggressive
    [[nodiscard]] static std::vector<ColumnDiff>
    compute_renames(
        const sea::domain::Entity& entity,
        const TableInfo& table_info
        );

    // Helpers publics (utilises par compute_column_diffs pour exclure
    // les colonnes deja renommees)
    [[nodiscard]] static bool
    field_was_renamed(
        const std::string& field_name,
        const std::vector<ColumnDiff>& renames
        );

    [[nodiscard]] static bool
    column_was_renamed_from(
        const std::string& mysql_column_name,
        const std::vector<ColumnDiff>& renames
        );

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

    [[nodiscard]] static bool
    column_has_simple_index(const TableInfo&, const std::string&);

    [[nodiscard]] static bool
    column_has_unique_index(const TableInfo&, const std::string&);

    [[nodiscard]] static std::string
    find_index_name_for_column(const TableInfo&, const std::string&, bool);

    [[nodiscard]] static bool
    column_is_fk(const sea::domain::Entity&, const std::string&);

    // helpers
    [[nodiscard]] static int
    score_rename_candidate(
        const sea::domain::Field& yaml_field,
        std::size_t yaml_position,
        const ColumnInfo& mysql_column,
        std::size_t mysql_position
        );
};

} // namespace sea::infrastructure::persistence::mysql