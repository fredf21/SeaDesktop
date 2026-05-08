#include "schema_differ.h"

#include "database_mappings/mysql_type_mapping.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <unordered_set>
#include <variant>

namespace sea::infrastructure::persistence::mysql {

namespace {

std::string to_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::size_t extract_varchar_size(const std::string& mysql_type)
{
    const auto lower = to_lower(mysql_type);
    if (lower.find("varchar") != 0) return 0;

    const auto open = mysql_type.find('(');
    const auto close = mysql_type.find(')');
    if (open == std::string::npos || close == std::string::npos) return 0;
    if (close <= open + 1) return 0;

    try {
        return std::stoul(mysql_type.substr(open + 1, close - open - 1));
    } catch (...) {
        return 0;
    }
}

std::string extract_type_base(const std::string& mysql_type)
{
    const auto lower = to_lower(mysql_type);
    const auto paren = lower.find('(');
    if (paren != std::string::npos) {
        return lower.substr(0, paren);
    }
    return lower;
}

std::string yaml_default_to_string(const sea::domain::DefaultValue& dv)
{
    if (std::holds_alternative<std::monostate>(dv)) return "";
    if (std::holds_alternative<std::string>(dv)) return std::get<std::string>(dv);
    if (std::holds_alternative<std::int64_t>(dv)) return std::to_string(std::get<std::int64_t>(dv));
    if (std::holds_alternative<double>(dv)) return std::to_string(std::get<double>(dv));
    if (std::holds_alternative<bool>(dv)) return std::get<bool>(dv) ? "1" : "0";
    return "";
}

} // namespace anonyme

// ─────────────────────────────────────────────────────────────
// field_to_target_type
// ─────────────────────────────────────────────────────────────
std::string SchemaDiffer::field_to_target_type(const sea::domain::Field& field)
{
    using sea::domain::FieldType;

    switch (field.type) {
    case FieldType::String: {
        if (field.max_length.has_value()) {
            const auto max = *field.max_length;
            if (max > 65535) return "longtext";
            if (max > 255) return "text";
            return "varchar(" + std::to_string(max) + ")";
        }
        return "varchar(255)";
    }
    default:
        return to_lower(std::string(sea::domain::to_mysql_type(field.type)));
    }
}

bool SchemaDiffer::types_are_compatible(
    const std::string& current_mysql_type,
    const std::string& target_mysql_type)
{
    auto normalize = [](std::string s) {
        s = to_lower(std::move(s));
        s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
        return s;
    };

    return normalize(current_mysql_type) == normalize(target_mysql_type);
}

bool SchemaDiffer::is_type_change_safe(
    const std::string& current_mysql_type,
    const sea::domain::Field& target_field)
{
    const auto current_base = extract_type_base(current_mysql_type);
    const auto target_type = field_to_target_type(target_field);
    const auto target_base = extract_type_base(target_type);

    if (current_base == "varchar" && target_base == "varchar") {
        const auto current_size = extract_varchar_size(current_mysql_type);
        const auto target_size = extract_varchar_size(target_type);
        return target_size >= current_size;
    }

    if ((current_base == "varchar" || current_base == "char")
        && (target_base == "text" || target_base == "longtext")) {
        return true;
    }

    if (current_base == "text" && target_base == "longtext") {
        return true;
    }

    if ((current_base == "int" || current_base == "tinyint" || current_base == "smallint")
        && target_base == "bigint") {
        return true;
    }

    if (current_base == target_base) {
        return true;
    }

    return false;
}

bool SchemaDiffer::column_is_fk(
    const sea::domain::Entity& entity,
    const std::string& column_name)
{
    for (const auto& relation : entity.relations) {
        if (relation.kind == sea::domain::RelationKind::BelongsTo
            && relation.fk_column == column_name) {
            return true;
        }
    }
    return false;
}

bool SchemaDiffer::column_has_simple_index(
    const TableInfo& table_info,
    const std::string& column_name)
{
    for (const auto& idx : table_info.indexes) {
        if (idx.is_primary) continue;
        if (idx.is_unique) continue;
        if (idx.columns.size() != 1) continue;
        if (idx.columns[0] == column_name) return true;
    }
    return false;
}

bool SchemaDiffer::column_has_unique_index(
    const TableInfo& table_info,
    const std::string& column_name)
{
    for (const auto& idx : table_info.indexes) {
        if (idx.is_primary) continue;
        if (!idx.is_unique) continue;
        if (idx.columns.size() != 1) continue;
        if (idx.columns[0] == column_name) return true;
    }
    return false;
}

std::string SchemaDiffer::find_index_name_for_column(
    const TableInfo& table_info,
    const std::string& column_name,
    bool look_for_unique)
{
    for (const auto& idx : table_info.indexes) {
        if (idx.is_primary) continue;
        if (idx.is_unique != look_for_unique) continue;
        if (idx.columns.size() != 1) continue;
        if (idx.columns[0] == column_name) return idx.name;
    }
    return "";
}

// ─────────────────────────────────────────────────────────────
// compute_column_diffs (Phase A + B.1)
// ─────────────────────────────────────────────────────────────
std::vector<ColumnDiff>
SchemaDiffer::compute_column_diffs(
    const sea::domain::Entity& entity,
    const TableInfo& table_info)
{
    std::vector<ColumnDiff> diffs;

    const std::string table_name =
        !entity.table_name.empty() ? entity.table_name : entity.name;

    for (const auto& field : entity.fields) {
        const ColumnInfo* current_column = table_info.find_column(field.name);

        if (current_column == nullptr) {
            ColumnDiff diff;
            diff.kind = ColumnDiffKind::Added;
            diff.table_name = table_name;
            diff.column_name = field.name;
            diff.target_field = &field;
            diff.is_safe = true;
            diff.description = "ADD COLUMN " + field.name;
            diffs.push_back(std::move(diff));
            continue;
        }

        const auto target_type = field_to_target_type(field);
        if (!types_are_compatible(current_column->column_type, target_type)) {
            ColumnDiff diff;
            diff.kind = ColumnDiffKind::TypeChanged;
            diff.table_name = table_name;
            diff.column_name = field.name;
            diff.current_type = current_column->column_type;
            diff.target_type = target_type;
            diff.target_field = &field;
            diff.is_safe = is_type_change_safe(current_column->column_type, field);

            std::ostringstream desc;
            desc << "MODIFY COLUMN " << field.name << ": "
                 << current_column->column_type << " → " << target_type
                 << (diff.is_safe ? " (safe)" : " (UNSAFE)");
            diff.description = desc.str();

            diffs.push_back(std::move(diff));
            continue;
        }

        const bool yaml_required = field.required || field.name == "id";
        const bool yaml_nullable = !yaml_required;
        const bool mysql_nullable = current_column->is_nullable;

        if (yaml_nullable != mysql_nullable) {
            ColumnDiff diff;
            diff.kind = ColumnDiffKind::NullabilityChanged;
            diff.table_name = table_name;
            diff.column_name = field.name;
            diff.target_field = &field;
            diff.is_safe = (yaml_nullable && !mysql_nullable);

            std::ostringstream desc;
            desc << "NULLABILITY: " << field.name
                 << (mysql_nullable ? " NULL → NOT NULL" : " NOT NULL → NULL")
                 << (diff.is_safe ? " (safe)" : " (UNSAFE)");
            diff.description = desc.str();

            diffs.push_back(std::move(diff));
        }

        const std::string yaml_default = yaml_default_to_string(field.default_val);
        const std::string mysql_default = current_column->default_value.value_or("");

        if (yaml_default != mysql_default) {
            ColumnDiff diff;
            diff.kind = ColumnDiffKind::DefaultChanged;
            diff.table_name = table_name;
            diff.column_name = field.name;
            diff.target_field = &field;
            diff.is_safe = true;

            std::ostringstream desc;
            desc << "DEFAULT: " << field.name << " '" << mysql_default
                 << "' → '" << yaml_default << "' (safe)";
            diff.description = desc.str();

            diffs.push_back(std::move(diff));
        }
    }

    return diffs;
}

// ─────────────────────────────────────────────────────────────
// compute_index_diffs (Phase B.2) - inchange depuis B.2
// ─────────────────────────────────────────────────────────────
std::vector<ColumnDiff>
SchemaDiffer::compute_index_diffs(
    const sea::domain::Entity& entity,
    const TableInfo& table_info)
{
    std::vector<ColumnDiff> diffs;

    const std::string table_name =
        !entity.table_name.empty() ? entity.table_name : entity.name;

    for (const auto& field : entity.fields) {
        if (field.name == "id") continue;
        if (!table_info.has_column(field.name)) continue;
        if (column_is_fk(entity, field.name)) continue;

        const bool yaml_unique = field.unique;
        const bool mysql_unique = column_has_unique_index(table_info, field.name);

        if (yaml_unique && !mysql_unique) {
            ColumnDiff diff;
            diff.kind = ColumnDiffKind::UniqueAdded;
            diff.table_name = table_name;
            diff.column_name = field.name;
            diff.target_field = &field;
            diff.is_safe = false;

            std::ostringstream desc;
            desc << "ADD UNIQUE: " << field.name << " (UNSAFE if duplicates exist)";
            diff.description = desc.str();
            diffs.push_back(std::move(diff));
        }
        else if (!yaml_unique && mysql_unique) {
            ColumnDiff diff;
            diff.kind = ColumnDiffKind::UniqueRemoved;
            diff.table_name = table_name;
            diff.column_name = field.name;
            diff.target_field = &field;
            diff.index_name_to_drop = find_index_name_for_column(table_info, field.name, true);
            diff.is_safe = true;

            std::ostringstream desc;
            desc << "DROP UNIQUE: " << field.name
                 << " (index `" << diff.index_name_to_drop << "`)";
            diff.description = desc.str();
            diffs.push_back(std::move(diff));
        }

        if (yaml_unique || mysql_unique) continue;

        const bool yaml_indexed = field.indexed;
        const bool mysql_indexed = column_has_simple_index(table_info, field.name);

        if (yaml_indexed && !mysql_indexed) {
            ColumnDiff diff;
            diff.kind = ColumnDiffKind::IndexAdded;
            diff.table_name = table_name;
            diff.column_name = field.name;
            diff.target_field = &field;
            diff.is_safe = true;

            std::ostringstream desc;
            desc << "ADD INDEX: " << field.name << " (safe)";
            diff.description = desc.str();
            diffs.push_back(std::move(diff));
        }
        else if (!yaml_indexed && mysql_indexed) {
            ColumnDiff diff;
            diff.kind = ColumnDiffKind::IndexRemoved;
            diff.table_name = table_name;
            diff.column_name = field.name;
            diff.target_field = &field;
            diff.index_name_to_drop = find_index_name_for_column(table_info, field.name, false);
            diff.is_safe = true;

            std::ostringstream desc;
            desc << "DROP INDEX: " << field.name
                 << " (index `" << diff.index_name_to_drop << "`, safe)";
            diff.description = desc.str();
            diffs.push_back(std::move(diff));
        }
    }

    return diffs;
}

// ═════════════════════════════════════════════════════════════
// ✨ PHASE B.3 : compute_renames + heuristique
// ═════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────
// score_rename_candidate (heuristique)
//
// Calcule un score de confiance (0-100) pour un candidat rename.
// Plus le score est haut, plus le rename est probable.
//
// Criteres (score additif) :
// - Type identique             : +50
// - Position similaire (+/-1)  : +30
// - Position identique         : bonus +5
// - Nullability identique      : +10
// - Default identique          : +5
// - Indexed/Unique identique   : +5
//
// Seuil pour rename automatique : 90+ (sinon traite comme drop+add).
// ─────────────────────────────────────────────────────────────
int SchemaDiffer::score_rename_candidate(
    const sea::domain::Field& yaml_field,
    std::size_t yaml_position,
    const ColumnInfo& mysql_column,
    std::size_t mysql_position)
{
    int score = 0;

    // ── Type (+50) ──
    const auto target_type = field_to_target_type(yaml_field);
    if (types_are_compatible(mysql_column.column_type, target_type)) {
        score += 50;
    } else {
        // Type compatible (ex: VARCHAR(50) → VARCHAR(255)) ?
        if (is_type_change_safe(mysql_column.column_type, yaml_field)) {
            score += 30;  // moins de poids car type a change
        }
        // Type vraiment different : pas de bonus du tout
    }

    // ── Position (+30 max) ──
    const auto pos_diff =
        (yaml_position > mysql_position)
            ? (yaml_position - mysql_position)
            : (mysql_position - yaml_position);

    if (pos_diff == 0) {
        score += 35;  // position EXACTE = bonus max
    } else if (pos_diff == 1) {
        score += 25;
    } else if (pos_diff == 2) {
        score += 15;
    }
    // pos_diff > 2 : pas de bonus

    // ── Nullability (+10) ──
    const bool yaml_nullable = !(yaml_field.required || yaml_field.name == "id");
    if (yaml_nullable == mysql_column.is_nullable) {
        score += 10;
    }

    // ── Default (+5) ──
    const std::string yaml_default = yaml_default_to_string(yaml_field.default_val);
    const std::string mysql_default = mysql_column.default_value.value_or("");
    if (yaml_default == mysql_default) {
        score += 5;
    }

    return score;
}

// ─────────────────────────────────────────────────────────────
// compute_renames
//
// Phase 1 : annotations explicites (previous_name)
// Phase 2 : heuristique automatique
// ─────────────────────────────────────────────────────────────
std::vector<ColumnDiff>
SchemaDiffer::compute_renames(
    const sea::domain::Entity& entity,
    const TableInfo& table_info)
{
    std::vector<ColumnDiff> renames;

    const std::string table_name =
        !entity.table_name.empty() ? entity.table_name : entity.name;

    // Tracking des colonnes deja matchees pour eviter les conflits
    std::unordered_set<std::string> matched_mysql_columns;
    std::unordered_set<std::string> matched_yaml_fields;

    // ════════════════════════════════════════════════════════
    // PHASE 1 : Annotations explicites (previous_name)
    // ════════════════════════════════════════════════════════
    for (const auto& field : entity.fields) {
        if (!field.has_previous_name()) continue;

        const std::string& old_name = *field.previous_name;
        const std::string& new_name = field.name;

        // Si la colonne old_name n'existe pas en MySQL, ignore silencieusement
        // (le rename a peut-etre deja ete applique)
        if (!table_info.has_column(old_name)) {
            std::cerr << "[DIFF] " << table_name << ": rename annotation "
                      << old_name << " → " << new_name
                      << " skipped (old column not found)\n";
            continue;
        }

        // Si la colonne new_name existe deja en MySQL → conflit
        if (table_info.has_column(new_name)) {
            std::cerr << "[DIFF] " << table_name << ": rename annotation "
                      << old_name << " → " << new_name
                      << " skipped (new column already exists, conflict)\n";
            continue;
        }

        ColumnDiff diff;
        diff.kind = ColumnDiffKind::Renamed;
        diff.table_name = table_name;
        diff.column_name = new_name;
        diff.previous_name = old_name;
        diff.target_field = &field;
        diff.rename_confidence_score = 100;  // explicite
        diff.is_safe = true;  // annotation explicite est toujours safe

        std::ostringstream desc;
        desc << "RENAME COLUMN " << old_name << " → " << new_name
             << " (explicit, safe)";
        diff.description = desc.str();

        renames.push_back(std::move(diff));
        matched_mysql_columns.insert(old_name);
        matched_yaml_fields.insert(new_name);
    }

    // ════════════════════════════════════════════════════════
    // PHASE 2 : Heuristique automatique
    // ════════════════════════════════════════════════════════

    // Trouve les colonnes MySQL "orphelines" (pas dans le YAML, pas matchees)
    std::vector<std::pair<std::size_t, const ColumnInfo*>> orphan_columns;
    for (std::size_t i = 0; i < table_info.columns.size(); ++i) {
        const auto& col = table_info.columns[i];
        if (matched_mysql_columns.count(col.name)) continue;

        // Verifie si un field YAML porte ce nom
        bool found_in_yaml = false;
        for (const auto& field : entity.fields) {
            if (field.name == col.name) {
                found_in_yaml = true;
                break;
            }
        }
        if (!found_in_yaml) {
            orphan_columns.emplace_back(i, &col);
        }
    }

    // Trouve les fields YAML "non-matches" (pas en MySQL, pas deja renames)
    std::vector<std::pair<std::size_t, const sea::domain::Field*>> unmatched_fields;
    for (std::size_t i = 0; i < entity.fields.size(); ++i) {
        const auto& field = entity.fields[i];
        if (matched_yaml_fields.count(field.name)) continue;
        if (table_info.has_column(field.name)) continue;
        unmatched_fields.emplace_back(i, &field);
    }

    // Pour chaque colonne orpheline, cherche le meilleur match dans les fields non-matches
    constexpr int RENAME_THRESHOLD = 90;

    for (const auto& [mysql_pos, mysql_col] : orphan_columns) {
        int best_score = 0;
        const sea::domain::Field* best_field = nullptr;
        std::size_t best_field_pos = 0;

        for (const auto& [yaml_pos, yaml_field] : unmatched_fields) {
            // Skip si deja matche
            if (matched_yaml_fields.count(yaml_field->name)) continue;

            const int score = score_rename_candidate(
                *yaml_field, yaml_pos, *mysql_col, mysql_pos
                );

            if (score > best_score) {
                best_score = score;
                best_field = yaml_field;
                best_field_pos = yaml_pos;
            }
        }

        // Si meilleur score >= seuil → rename heuristique
        if (best_score >= RENAME_THRESHOLD && best_field != nullptr) {
            ColumnDiff diff;
            diff.kind = ColumnDiffKind::Renamed;
            diff.table_name = table_name;
            diff.column_name = best_field->name;
            diff.previous_name = mysql_col->name;
            diff.target_field = best_field;
            diff.rename_confidence_score = best_score;
            diff.is_safe = false;  // heuristique = pas safe (incertain)

            std::ostringstream desc;
            desc << "RENAME COLUMN " << mysql_col->name
                 << " → " << best_field->name
                 << " (heuristic, score=" << best_score << "/100, UNSAFE)";
            diff.description = desc.str();

            renames.push_back(std::move(diff));
            matched_mysql_columns.insert(mysql_col->name);
            matched_yaml_fields.insert(best_field->name);
        }
    }

    return renames;
}

// ─────────────────────────────────────────────────────────────
// Helpers publics : verifie si un field/column a ete renomme
// (pour que compute_column_diffs et compute_index_diffs ignorent
//  les colonnes deja gerees par le rename)
// ─────────────────────────────────────────────────────────────
bool SchemaDiffer::field_was_renamed(
    const std::string& field_name,
    const std::vector<ColumnDiff>& renames)
{
    for (const auto& r : renames) {
        if (r.kind == ColumnDiffKind::Renamed && r.column_name == field_name) {
            return true;
        }
    }
    return false;
}

bool SchemaDiffer::column_was_renamed_from(
    const std::string& mysql_column_name,
    const std::vector<ColumnDiff>& renames)
{
    for (const auto& r : renames) {
        if (r.kind == ColumnDiffKind::Renamed && r.previous_name == mysql_column_name) {
            return true;
        }
    }
    return false;
}

} // namespace sea::infrastructure::persistence::mysql