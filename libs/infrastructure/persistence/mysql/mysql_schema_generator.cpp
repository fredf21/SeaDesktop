#include "mysql_schema_generator.h"

#include "database_mappings/mysql_type_mapping.h"
#include "sea_files_table.h"     // pour SeaFilesTable::TABLE_NAME et COL_ID
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <queue>
namespace sea::infrastructure::persistence::mysql {

namespace {

// ─────────────────────────────────────────────────────────────
// Convertit OnDelete enum en string SQL
// ─────────────────────────────────────────────────────────────
std::string on_delete_to_sql(sea::domain::OnDelete rule)
{
    switch (rule) {
    case sea::domain::OnDelete::Cascade:  return "CASCADE";
    case sea::domain::OnDelete::SetNull:  return "SET NULL";
    case sea::domain::OnDelete::Restrict: return "RESTRICT";
    }
    return "RESTRICT";
}

// ─────────────────────────────────────────────────────────────
// Convertit OnDeleteFile (config d'un champ File) en clause
// ON DELETE SQL pour la FK vers sea_files.
//
// Note importante : la FK SQL ne gère que la partie "row" du
// comportement. Le reste (décrément du reference_count, suppression
// physique du fichier sur disque) est orchestré au niveau applicatif
// par le FileService (Étape 6). Donc :
//
//   Cascade  → ON DELETE CASCADE  : la row de sea_files est supprimée
//              automatiquement quand la dernière entité référençante
//              l'est. Le FileService observera reference_count==0
//              et supprimera le fichier physique.
//              EN PRATIQUE : le FileService décrémente le compteur en
//              amont. Si on s'appuyait uniquement sur la FK CASCADE,
//              on perdrait la possibilité de partager (le delete d'une
//              entité supprimerait la row sea_files même si d'autres
//              entités y pointent encore). C'est pour cela qu'on
//              utilise RESTRICT côté SQL pour Cascade (cf. ci-dessous).
//
//   SetNull  → ON DELETE SET NULL : la colonne FK de l'entité passe à
//              NULL quand la row sea_files disparaît. Le FileService
//              décide quand supprimer la row sea_files.
//
//   Restrict → ON DELETE RESTRICT : impossible de supprimer une row
//              sea_files tant qu'une entité y pointe. Stop net.
//
// CHOIX FINAL : pour Cascade ET Restrict, on utilise RESTRICT au
// niveau SQL. La différence Cascade/Restrict est appliquée par le
// FileService en amont (Cascade décrémente le compteur ; Restrict
// refuse le DELETE de l'entité). SET NULL est mappé directement.
// ─────────────────────────────────────────────────────────────
std::string on_delete_file_to_sql(sea::domain::OnDeleteFile rule)
{
    switch (rule) {
    case sea::domain::OnDeleteFile::Cascade:  return "RESTRICT";
    case sea::domain::OnDeleteFile::SetNull:  return "SET NULL";
    case sea::domain::OnDeleteFile::Restrict: return "RESTRICT";
    }
    return "RESTRICT";
}

// ─────────────────────────────────────────────────────────────
// Convertit une DefaultValue en litteral SQL
// ─────────────────────────────────────────────────────────────
std::string default_value_to_sql(const sea::domain::DefaultValue& dv)
{
    if (std::holds_alternative<std::string>(dv)) {
        // Echapper les quotes simples : ' → ''
        std::string s = std::get<std::string>(dv);
        std::string escaped;
        escaped.reserve(s.size() + 2);
        escaped += "'";
        for (char c : s) {
            if (c == '\'') escaped += "''";
            else escaped += c;
        }
        escaped += "'";
        return escaped;
    }
    if (std::holds_alternative<std::int64_t>(dv)) {
        return std::to_string(std::get<std::int64_t>(dv));
    }
    if (std::holds_alternative<double>(dv)) {
        return std::to_string(std::get<double>(dv));
    }
    if (std::holds_alternative<bool>(dv)) {
        return std::get<bool>(dv) ? "TRUE" : "FALSE";
    }
    return "NULL";
}

// ─────────────────────────────────────────────────────────────
// Genere le mapping precis du type MySQL en consultant max_length.
// (utilise par column_definition)
// ─────────────────────────────────────────────────────────────
std::string mysql_column_type_for_field(const sea::domain::Field& field)
{
    using sea::domain::FieldType;

    switch (field.type) {
    case FieldType::String: {
        if (field.max_length.has_value()) {
            const auto max = *field.max_length;
            if (max > 65535) return "LONGTEXT";
            if (max > 255) return "TEXT";
            return "VARCHAR(" + std::to_string(max) + ")";
        }
        return "VARCHAR(255)";
    }
    // ── Types numeriques entiers : suffixe UNSIGNED si demande ──
    case FieldType::Int:
    case FieldType::BigInt:
    case FieldType::SmallInt: {
        std::string base = std::string(sea::domain::to_mysql_type(field.type));
        if (field.unsigned_value) {
            base += " UNSIGNED";
        }
        return base;
    }

    // ── Decimal et Float peuvent aussi etre UNSIGNED en MySQL ──
    case FieldType::Decimal:
    case FieldType::Float: {
        std::string base = std::string(sea::domain::to_mysql_type(field.type));
        if (field.unsigned_value) {
            base += " UNSIGNED";
        }
        return base;
    }
    default:
        // Tous les autres types : utilise le mapping basique
        return std::string(sea::domain::to_mysql_type(field.type));
    }
}

} // namespace anonyme

// ─────────────────────────────────────────────────────────────
// generate_create_database_sql
// ─────────────────────────────────────────────────────────────
std::string MysqlSchemaGenerator::generate_create_database_sql(
    const std::string& database_name)
{
    std::ostringstream sql;
    sql << "CREATE DATABASE IF NOT EXISTS `" << database_name << "` "
        << "DEFAULT CHARACTER SET utf8mb4 "
        << "DEFAULT COLLATE utf8mb4_unicode_ci";
    return sql.str();
}

// ─────────────────────────────────────────────────────────────
// resolve_table_name
// ─────────────────────────────────────────────────────────────
std::string MysqlSchemaGenerator::resolve_table_name(const sea::domain::Entity& entity)
{
    return !entity.table_name.empty() ? entity.table_name : sea::domain::Entity::to_route_plural(entity.name);
}

// ─────────────────────────────────────────────────────────────
// column_definition - definit une colonne SQL
// ─────────────────────────────────────────────────────────────
std::string MysqlSchemaGenerator::column_definition(
    const sea::domain::Field& field,
    bool is_id)
{
    std::ostringstream sql;
    sql << "`" << field.name << "` " << mysql_column_type_for_field(field);

    // NOT NULL
    if (field.required || is_id) {
        sql << " NOT NULL";
    }

    // AUTO_INCREMENT (uniquement pour id Int)
    if (is_id && sea::domain::mysql_supports_auto_increment(field.type)) {
        sql << " AUTO_INCREMENT";
    }

    // DEFAULT
    if (field.has_default()) {
        sql << " DEFAULT " << default_value_to_sql(field.default_val);
    }

    return sql.str();
}

// ─────────────────────────────────────────────────────────────
// generate_create_table_sql
// ─────────────────────────────────────────────────────────────
std::string MysqlSchemaGenerator::generate_create_table_sql(
    const sea::domain::Entity& entity)
{
    const std::string table_name = resolve_table_name(entity);

    std::ostringstream sql;
    sql << "CREATE TABLE IF NOT EXISTS `" << table_name << "` (\n";

    // ── 1. Colonnes ─────────────────────────────────────────
    bool first = true;
    for (const auto& field : entity.fields) {
        if (!first) sql << ",\n";
        first = false;

        const bool is_id = (field.name == "id");
        sql << "  " << column_definition(field, is_id);
    }

    // ── 2. PRIMARY KEY ──────────────────────────────────────
    if (entity.has_field("id")) {
        sql << ",\n  PRIMARY KEY (`id`)";
    }

    // ── 3. UNIQUE constraints ───────────────────────────────
    for (const auto& field : entity.fields) {
        if (field.unique && field.name != "id") {  // id est deja PK
            sql << ",\n  UNIQUE KEY `uk_" << field.name << "` (`" << field.name << "`)";
        }
    }

    // ── 4. INDEX ────────────────────────────────────────────
    for (const auto& field : entity.fields) {
        if (field.indexed && !field.unique && field.name != "id") {
            sql << ",\n  INDEX `idx_" << field.name << "` (`" << field.name << "`)";
        }
    }

    // ── 5. FOREIGN KEY (depuis BelongsTo) ───────────────────
    for (const auto& relation : entity.relations) {
        if (relation.kind != sea::domain::RelationKind::BelongsTo) {
            continue;
        }
        if (relation.fk_column.empty()) {
            continue;
        }
        const std::string target_table =
            sea::domain::Entity::to_route_plural(relation.target_entity);
        // Index sur la FK (perfs des JOIN)
        sql << ",\n  INDEX `idx_" << relation.fk_column
            << "` (`" << relation.fk_column << "`)";

        // Contrainte FK
        sql << ",\n  CONSTRAINT `fk_" << table_name << "_" << relation.fk_column << "`"
            << "\n    FOREIGN KEY (`" << relation.fk_column << "`)"
            << "\n    REFERENCES `" << target_table << "` (`id`)"
            << "\n    ON DELETE " << on_delete_to_sql(relation.on_delete);
    }

    // ── 6. FOREIGN KEY (depuis les champs File) ─────────────
    //
    // Chaque champ File référence sea_files.id via une FK avec
    // ON DELETE dérivé du FileFieldConfig::on_delete.
    //
    // Pas d'index séparé : la FK MySQL crée automatiquement un
    // index sur la colonne référençante (ce qui n'est pas le cas
    // dans tous les SGBD, mais MySQL le fait).
    //
    // Le validator (Étape 3) garantit que tout champ File a un
    // file_config non vide ; on protège quand même avec un check
    // défensif (is_file_field) pour éviter tout crash si un schéma
    // construit en C++ direct contournait le validator.
    for (const auto& field : entity.fields) {
        if (!field.is_file_field()) {
            continue;
        }

        const auto& cfg = *field.file_config;

        sql << ",\n  CONSTRAINT `fk_" << table_name << "_" << field.name << "_file`"
            << "\n    FOREIGN KEY (`" << field.name << "`)"
            << "\n    REFERENCES `" << SeaFilesTable::TABLE_NAME
            << "` (`" << SeaFilesTable::COL_ID << "`)"
            << "\n    ON DELETE " << on_delete_file_to_sql(cfg.on_delete);
    }

    sql << "\n) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci";
    return sql.str();
}

// ─────────────────────────────────────────────────────────────
// generate_add_column_sql
// ─────────────────────────────────────────────────────────────
std::string MysqlSchemaGenerator::generate_add_column_sql(
    const sea::domain::Entity& entity,
    const sea::domain::Field& field)
{
    const std::string table_name = resolve_table_name(entity);
    const bool is_id = (field.name == "id");

    std::ostringstream sql;
    sql << "ALTER TABLE `" << table_name << "` "
        << "ADD COLUMN " << column_definition(field, is_id);

    // Si UNIQUE, on ajoute la contrainte separement
    if (field.unique && !is_id) {
        // Pour une simple ADD COLUMN, on ne peut pas mettre UNIQUE
        // dans la meme commande facilement → 2 statements separes.
        // Le bootstrapper va appliquer cela apres.
    }

    return sql.str();
}

// ─────────────────────────────────────────────────────────────
// generate_modify_column_sql (Phase B.1)
//
// Genere "ALTER TABLE x MODIFY COLUMN y type [NOT NULL] [DEFAULT ...]"
//
// MODIFY COLUMN reecrit TOUTE la definition de la colonne, donc on
// reutilise column_definition() qui inclut deja type/NOT NULL/DEFAULT.
// ─────────────────────────────────────────────────────────────
std::string MysqlSchemaGenerator::generate_modify_column_sql(
    const sea::domain::Entity& entity,
    const sea::domain::Field& field)
{
    const std::string table_name = resolve_table_name(entity);
    const bool is_id = (field.name == "id");

    std::ostringstream sql;
    sql << "ALTER TABLE `" << table_name << "` "
        << "MODIFY COLUMN " << column_definition(field, is_id);

    return sql.str();
}

// ─────────────────────────────────────────────────────────────
// generate_add_index_sql (Phase B.2)
//
// Convention : INDEX nomme `idx_<column>` (coherent avec Phase A).
// ─────────────────────────────────────────────────────────────
std::string MysqlSchemaGenerator::generate_add_index_sql(
    const std::string& table_name,
    const std::string& column_name)
{
    std::ostringstream sql;
    sql << "ALTER TABLE `" << table_name << "` "
        << "ADD INDEX `idx_" << column_name << "` (`" << column_name << "`)";
    return sql.str();
}

// ─────────────────────────────────────────────────────────────
// generate_drop_index_sql
// ─────────────────────────────────────────────────────────────
std::string MysqlSchemaGenerator::generate_drop_index_sql(
    const std::string& table_name,
    const std::string& index_name)
{
    std::ostringstream sql;
    sql << "ALTER TABLE `" << table_name << "` "
        << "DROP INDEX `" << index_name << "`";
    return sql.str();
}

// ─────────────────────────────────────────────────────────────
// generate_add_unique_sql
//
// Convention : UNIQUE constraint nomme `uk_<column>`.
// ─────────────────────────────────────────────────────────────
std::string MysqlSchemaGenerator::generate_add_unique_sql(
    const std::string& table_name,
    const std::string& column_name)
{
    std::ostringstream sql;
    sql << "ALTER TABLE `" << table_name << "` "
        << "ADD UNIQUE KEY `uk_" << column_name << "` (`" << column_name << "`)";
    return sql.str();
}

// ─────────────────────────────────────────────────────────────
// generate_drop_unique_sql
//
// Note : MySQL traite UNIQUE comme un INDEX, donc DROP INDEX
//        marche aussi pour les UNIQUE.
// ─────────────────────────────────────────────────────────────
std::string MysqlSchemaGenerator::generate_drop_unique_sql(
    const std::string& table_name,
    const std::string& constraint_name)
{
    std::ostringstream sql;
    sql << "ALTER TABLE `" << table_name << "` "
        << "DROP INDEX `" << constraint_name << "`";
    return sql.str();
}
// ─────────────────────────────────────────────────────────────
// generate_rename_column_sql
//
// Genere :
//   ALTER TABLE `table` CHANGE COLUMN `old_name` `new_name` <type> [NOT NULL] [DEFAULT ...]
//
// Le CHANGE COLUMN reecrit la definition complete (type/null/default)
// donc on reutilise column_definition().
// ─────────────────────────────────────────────────────────────
std::string MysqlSchemaGenerator::generate_rename_column_sql(
    const sea::domain::Entity& entity,
    const std::string& old_column_name,
    const sea::domain::Field& new_field)
{
    const std::string table_name = resolve_table_name(entity);
    const bool is_id = (new_field.name == "id");

    std::ostringstream sql;
    sql << "ALTER TABLE `" << table_name << "` "
        << "CHANGE COLUMN `" << old_column_name << "` "
        << column_definition(new_field, is_id);

    return sql.str();
}


// ─────────────────────────────────────────────────────────────
// generate_pivot_table_sql
// ─────────────────────────────────────────────────────────────
std::string MysqlSchemaGenerator::generate_pivot_table_sql(
    const std::string& pivot_table_name,
    const std::string& source_fk,
    const std::string& target_fk,
    const std::string& source_referenced_table,
    const std::string& target_referenced_table,
    sea::domain::OnDelete on_delete)
{
    std::ostringstream sql;
    sql << "CREATE TABLE IF NOT EXISTS `" << pivot_table_name << "` (\n";
    sql << "  `" << source_fk << "` BINARY(16) NOT NULL,\n";
    sql << "  `" << target_fk << "` BINARY(16) NOT NULL,\n";
    sql << "  PRIMARY KEY (`" << source_fk << "`, `" << target_fk << "`),\n";
    sql << "  INDEX `idx_" << source_fk << "` (`" << source_fk << "`),\n";
    sql << "  INDEX `idx_" << target_fk << "` (`" << target_fk << "`),\n";
    sql << "  CONSTRAINT `fk_" << pivot_table_name << "_" << source_fk << "`\n";
    sql << "    FOREIGN KEY (`" << source_fk << "`)\n";
    sql << "    REFERENCES `" << source_referenced_table << "` (`id`)\n";
    sql << "    ON DELETE " << on_delete_to_sql(on_delete) << ",\n";
    sql << "  CONSTRAINT `fk_" << pivot_table_name << "_" << target_fk << "`\n";
    sql << "    FOREIGN KEY (`" << target_fk << "`)\n";
    sql << "    REFERENCES `" << target_referenced_table << "` (`id`)\n";
    sql << "    ON DELETE " << on_delete_to_sql(on_delete) << "\n";
    sql << ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci";
    return sql.str();
}

// ─────────────────────────────────────────────────────────────
// topological_sort - tri Kahn
//
// Ordre les entites selon leurs FK BelongsTo :
// - Department (pas de FK) → niveau 0
// - Employee (FK vers Department) → niveau 1
// - StudentProgram (FK vers Student et Program) → apres ces 2
//
// Algorithme :
// 1. Calcule le degre entrant de chaque entite (= nb de FK BelongsTo)
// 2. Met les entites de degre 0 dans une queue
// 3. Pour chaque entite extraite, decremente le degre de celles qui pointent vers elle
// 4. Repete jusqu'a vider la queue
//
// Si cycle detecte (queue vide mais entities restantes) → on retourne quand meme,
// le cycle sera resolu via ALTER TABLE ADD CONSTRAINT plus tard.
// ─────────────────────────────────────────────────────────────
std::vector<const sea::domain::Entity*>
MysqlSchemaGenerator::topological_sort(const std::vector<sea::domain::Entity>& entities)
{
    // Map : nom entity → pointer vers l'entity
    std::unordered_map<std::string, const sea::domain::Entity*> entity_by_name;
    for (const auto& entity : entities) {
        entity_by_name[entity.name] = &entity;
    }

    // Map : nom entity → ses dependances (entites qu'elle reference via BelongsTo)
    std::unordered_map<std::string, std::unordered_set<std::string>> dependencies;
    for (const auto& entity : entities) {
        auto& deps = dependencies[entity.name];
        for (const auto& relation : entity.relations) {
            if (relation.kind == sea::domain::RelationKind::BelongsTo) {
                deps.insert(relation.target_entity);
            }
        }
    }

    // Map : nom entity → nb de dependances restantes (degre entrant)
    std::unordered_map<std::string, std::size_t> in_degree;
    for (const auto& [name, deps] : dependencies) {
        in_degree[name] = deps.size();
    }

    // Queue des entites pretes a etre creees (degre 0)
    std::queue<std::string> ready_queue;
    for (const auto& [name, degree] : in_degree) {
        if (degree == 0) {
            ready_queue.push(name);
        }
    }

    // Resultat
    std::vector<const sea::domain::Entity*> sorted;
    sorted.reserve(entities.size());

    // Set : entites deja traitees
    std::unordered_set<std::string> processed;

    while (!ready_queue.empty()) {
        const std::string current = ready_queue.front();
        ready_queue.pop();

        if (processed.count(current)) continue;
        processed.insert(current);

        // Ajoute a la liste triee
        const auto* entity = entity_by_name[current];
        if (entity != nullptr) {
            sorted.push_back(entity);
        }

        // Pour chaque entite qui depend de current, decremente son degre
        for (auto& [name, deps] : dependencies) {
            if (deps.count(current)) {
                deps.erase(current);
                if (deps.empty() && !processed.count(name)) {
                    ready_queue.push(name);
                }
            }
        }
    }

    // Si certaines entites n'ont pas pu etre traitees (cycle detecte),
    // on les ajoute quand meme a la fin (le bootstrapper les creera sans FK
    // puis ajoutera les FK apres).
    for (const auto& entity : entities) {
        if (!processed.count(entity.name)) {
            sorted.push_back(&entity);
        }
    }

    return sorted;
}

} // namespace sea::infrastructure::persistence::mysql