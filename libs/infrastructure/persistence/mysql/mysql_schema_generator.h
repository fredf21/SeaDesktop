#pragma once

#include "entity.h"
#include "field.h"
#include "relation.h"

#include <string>
#include <vector>

namespace sea::infrastructure::persistence::mysql {

// ─────────────────────────────────────────────────────────────
// MysqlSchemaGenerator
//
// Genere les statements SQL DDL pour MySQL a partir du schema
// declare dans le YAML (sea::domain::Schema).
//
// Statements generes :
// - CREATE DATABASE IF NOT EXISTS <name>
// - CREATE TABLE IF NOT EXISTS <name> (...) avec PK, UNIQUE, INDEX, FK
// - ALTER TABLE <name> ADD COLUMN <field> ...
// - CREATE TABLE IF NOT EXISTS <pivot_name> (...) pour M2M
//
// Note : le generator ne fait que produire des STRINGS. Il ne se
// connecte pas a MySQL et ne sait rien de l'etat actuel.
// L'orchestration (Bootstrapper) decide quels statements executer.
// ─────────────────────────────────────────────────────────────
class MysqlSchemaGenerator {
public:
    // Genere "CREATE DATABASE IF NOT EXISTS `name`"
    [[nodiscard]] static std::string
    generate_create_database_sql(const std::string& database_name);

    // Genere "CREATE TABLE IF NOT EXISTS ..." pour une entite.
    // Inclut PK, UNIQUE, INDEX, FK.
    [[nodiscard]] static std::string
    generate_create_table_sql(const sea::domain::Entity& entity);

    // Genere "ALTER TABLE x ADD COLUMN y ..."
    [[nodiscard]] static std::string
    generate_add_column_sql(
        const sea::domain::Entity& entity,
        const sea::domain::Field& field
        );

    // Genere "ALTER TABLE x MODIFY COLUMN y ..."
    //
    // Utilise pour appliquer un changement de type, de nullability
    // ou de default sur une colonne existante.
    //
    // Note : MODIFY COLUMN reecrit la definition complete de la colonne.
    //        Donc on inclut toujours le type + NOT NULL + DEFAULT.
    [[nodiscard]] static std::string
    generate_modify_column_sql(
        const sea::domain::Entity& entity,
        const sea::domain::Field& field
        );

    //INDEX (non-unique)
    [[nodiscard]] static std::string
    generate_add_index_sql(
        const std::string& table_name,
        const std::string& column_name
        );

    [[nodiscard]] static std::string
    generate_drop_index_sql(
        const std::string& table_name,
        const std::string& index_name
        );

    //UNIQUE constraint
    [[nodiscard]] static std::string
    generate_add_unique_sql(
        const std::string& table_name,
        const std::string& column_name
        );

    [[nodiscard]] static std::string
    generate_drop_unique_sql(
        const std::string& table_name,
        const std::string& constraint_name
        );

    // RENAME COLUMN
    //
    // Genere "ALTER TABLE x CHANGE COLUMN old_name new_name type [NOT NULL] [DEFAULT ...]"
    //
    // Utilise CHANGE COLUMN (et non RENAME COLUMN) pour 2 raisons :
    // 1. Compatible MySQL 5.7+ (RENAME COLUMN est MySQL 8+)
    // 2. Permet de modifier le type au passage si necessaire
    //
    // IMPORTANT : preserve TOUTES les donnees (juste un rename).
    [[nodiscard]] static std::string
    generate_rename_column_sql(
        const sea::domain::Entity& entity,
        const std::string& old_column_name,
        const sea::domain::Field& new_field
        );

    // Genere "CREATE TABLE IF NOT EXISTS pivot_name (..)" pour M2M.
    // pivot_table_name : ex "StudentProgram"
    // source_fk : ex "student_id"
    // target_fk : ex "program_id"
    [[nodiscard]] static std::string
    generate_pivot_table_sql(
        const std::string& pivot_table_name,
        const std::string& source_fk,
        const std::string& target_fk,
        const std::string& source_referenced_table,  // "Student"
        const std::string& target_referenced_table,  // "Program"
        sea::domain::OnDelete on_delete = sea::domain::OnDelete::Cascade
        );

    // Tri topologique des entites selon leurs FK BelongsTo.
    // Retourne les entites dans l'ordre de creation correct
    // (parents avant enfants).
    //
    // Ex: Department avant Employee
    //     Student et Program avant StudentProgram (pivot)
    [[nodiscard]] static std::vector<const sea::domain::Entity*>
    topological_sort(const std::vector<sea::domain::Entity>& entities);

private:
    // Helpers internes pour construire les morceaux du SQL
    [[nodiscard]] static std::string
    column_definition(const sea::domain::Field& field, bool is_id);

    [[nodiscard]] static std::string
    resolve_table_name(const sea::domain::Entity& entity);
};

} // namespace sea::infrastructure::persistence::mysql