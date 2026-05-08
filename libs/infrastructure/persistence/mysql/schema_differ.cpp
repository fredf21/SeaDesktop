#include "schema_differ.h"

#include "database_mappings/mysql_type_mapping.h"

#include <algorithm>
#include <cctype>
#include <sstream>
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

// ─────────────────────────────────────────────────────────────
// types_are_compatible
// ─────────────────────────────────────────────────────────────
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

// ─────────────────────────────────────────────────────────────
// is_type_change_safe
// ─────────────────────────────────────────────────────────────
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

// ─────────────────────────────────────────────────────────────
// compute_column_diffs (Phase B.1)
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

// ═════════════════════════════════════════════════════════════
// ✨ PHASE B.2 : compute_index_diffs et helpers
// ═════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────
// column_is_fk : la colonne est-elle une FK BelongsTo ?
//
// Si oui, MySQL a deja un INDEX implicite, on ne le DROP jamais
// pour eviter de casser la FK.
// ─────────────────────────────────────────────────────────────
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

// ─────────────────────────────────────────────────────────────
// column_has_simple_index : la colonne a-t-elle un INDEX non-unique ?
//
// On exclut PRIMARY (= PK) et les indexes UNIQUE.
// On regarde uniquement les indexes simple-colonne (pas multi-colonnes).
// ─────────────────────────────────────────────────────────────
bool SchemaDiffer::column_has_simple_index(
    const TableInfo& table_info,
    const std::string& column_name)
{
    for (const auto& idx : table_info.indexes) {
        if (idx.is_primary) continue;
        if (idx.is_unique) continue;
        if (idx.columns.size() != 1) continue;
        if (idx.columns[0] == column_name) {
            return true;
        }
    }
    return false;
}

// ─────────────────────────────────────────────────────────────
// column_has_unique_index : la colonne a-t-elle une contrainte UNIQUE ?
//
// On regarde les indexes non-PK avec NON_UNIQUE=0 sur cette colonne.
// ─────────────────────────────────────────────────────────────
bool SchemaDiffer::column_has_unique_index(
    const TableInfo& table_info,
    const std::string& column_name)
{
    for (const auto& idx : table_info.indexes) {
        if (idx.is_primary) continue;
        if (!idx.is_unique) continue;
        if (idx.columns.size() != 1) continue;
        if (idx.columns[0] == column_name) {
            return true;
        }
    }
    return false;
}

// ─────────────────────────────────────────────────────────────
// find_index_name_for_column : retrouve le nom de l'index sur cette colonne
//
// look_for_unique=true : cherche un index UNIQUE
// look_for_unique=false : cherche un INDEX simple non-unique
// ─────────────────────────────────────────────────────────────
std::string SchemaDiffer::find_index_name_for_column(
    const TableInfo& table_info,
    const std::string& column_name,
    bool look_for_unique)
{
    for (const auto& idx : table_info.indexes) {
        if (idx.is_primary) continue;
        if (idx.is_unique != look_for_unique) continue;
        if (idx.columns.size() != 1) continue;
        if (idx.columns[0] == column_name) {
            return idx.name;
        }
    }
    return "";
}

// ─────────────────────────────────────────────────────────────
// compute_index_diffs (Phase B.2)
//
// Pour chaque field YAML, compare :
// - field.indexed avec presence d'un INDEX en MySQL
// - field.unique avec presence d'un UNIQUE en MySQL
//
// IMPORTANT :
// - On ne touche JAMAIS aux indexes des FK (auto-generes par MySQL,
//   les supprimer casserait la contrainte FK).
// - On ne touche JAMAIS au PRIMARY KEY (= la colonne id).
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
        // Skip 'id' (gere par PRIMARY KEY)
        if (field.name == "id") continue;

        // Skip si pas en MySQL (Added sera traite par column_diffs)
        if (!table_info.has_column(field.name)) continue;

        // Skip si c'est une FK (l'index est auto-genere par MySQL)
        if (column_is_fk(entity, field.name)) continue;

        // ─── Detection UNIQUE ───
        const bool yaml_unique = field.unique;
        const bool mysql_unique = column_has_unique_index(table_info, field.name);

        if (yaml_unique && !mysql_unique) {
            ColumnDiff diff;
            diff.kind = ColumnDiffKind::UniqueAdded;
            diff.table_name = table_name;
            diff.column_name = field.name;
            diff.target_field = &field;
            diff.is_safe = false;  // Peut echouer si doublons existent

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
            diff.is_safe = true;  // Drop UNIQUE est toujours safe

            std::ostringstream desc;
            desc << "DROP UNIQUE: " << field.name
                 << " (index `" << diff.index_name_to_drop << "`)";
            diff.description = desc.str();
            diffs.push_back(std::move(diff));
        }

        // ─── Detection INDEX (non-unique) ───
        // Note : si la colonne a un UNIQUE, on ne genere pas en plus un INDEX
        // (UNIQUE est deja un index).
        if (yaml_unique || mysql_unique) {
            // On a deja gere le UNIQUE, on skip la detection INDEX
            continue;
        }

        const bool yaml_indexed = field.indexed;
        const bool mysql_indexed = column_has_simple_index(table_info, field.name);

        if (yaml_indexed && !mysql_indexed) {
            ColumnDiff diff;
            diff.kind = ColumnDiffKind::IndexAdded;
            diff.table_name = table_name;
            diff.column_name = field.name;
            diff.target_field = &field;
            diff.is_safe = true;  // Ajouter un INDEX est toujours safe

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
            diff.is_safe = true;  // Drop INDEX est toujours safe

            std::ostringstream desc;
            desc << "DROP INDEX: " << field.name
                 << " (index `" << diff.index_name_to_drop << "`, safe)";
            diff.description = desc.str();
            diffs.push_back(std::move(diff));
        }
    }

    return diffs;
}

} // namespace sea::infrastructure::persistence::mysql