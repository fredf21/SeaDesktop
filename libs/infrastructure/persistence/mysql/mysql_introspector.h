#pragma once

#include "thread_pool_execution/i_blocking_executor.h"
#include "mysqlconnexionpool.h"

#include <cppconn/connection.h>

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <seastar/core/future.hh>
#include <seastar/core/sharded.hh>

namespace sea::infrastructure::persistence::mysql {

// ─────────────────────────────────────────────────────────────
// Informations sur une colonne dans MySQL
// (lues depuis INFORMATION_SCHEMA.COLUMNS)
// ─────────────────────────────────────────────────────────────
struct ColumnInfo {
    std::string name;
    std::string data_type;     // ex: "VARCHAR", "BIGINT", "BINARY"
    std::string column_type;   // ex: "VARCHAR(255)", "BINARY(16)"
    bool is_nullable = true;
    bool is_primary_key = false;
    bool is_unique = false;
    bool is_auto_increment = false;
    std::optional<std::string> default_value;
};

// ─────────────────────────────────────────────────────────────
// Informations sur un index
// ─────────────────────────────────────────────────────────────
struct IndexInfo {
    std::string name;
    std::vector<std::string> columns;
    bool is_unique = false;
    bool is_primary = false;
};

// ─────────────────────────────────────────────────────────────
// Informations sur une cle etrangere
// ─────────────────────────────────────────────────────────────
struct ForeignKeyInfo {
    std::string constraint_name;
    std::string column_name;
    std::string referenced_table;
    std::string referenced_column;
    std::string on_delete_action;  // CASCADE, RESTRICT, SET NULL, NO ACTION
};

// ─────────────────────────────────────────────────────────────
// Informations sur une table complete
// ─────────────────────────────────────────────────────────────
struct TableInfo {
    std::string name;
    std::vector<ColumnInfo> columns;
    std::vector<IndexInfo> indexes;
    std::vector<ForeignKeyInfo> foreign_keys;

    [[nodiscard]] const ColumnInfo* find_column(const std::string& col_name) const noexcept {
        for (const auto& col : columns) {
            if (col.name == col_name) {
                return &col;
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool has_column(const std::string& col_name) const noexcept {
        return find_column(col_name) != nullptr;
    }
};

// ─────────────────────────────────────────────────────────────
// Snapshot complet du schema MySQL
// ─────────────────────────────────────────────────────────────
struct SchemaSnapshot {
    std::string database_name;
    std::unordered_map<std::string, TableInfo> tables;

    [[nodiscard]] bool has_table(const std::string& table_name) const noexcept {
        return tables.find(table_name) != tables.end();
    }

    [[nodiscard]] const TableInfo* find_table(const std::string& table_name) const noexcept {
        auto it = tables.find(table_name);
        return it != tables.end() ? &it->second : nullptr;
    }
};

// ─────────────────────────────────────────────────────────────
// MysqlIntrospector
//
// Lit l'etat actuel d'une base MySQL :
// - Existence de la database
// - Tables presentes
// - Colonnes de chaque table
// - Index et cles etrangeres
//
// Utilise par le MysqlBootstrapper pour comparer avec le schema YAML.
//
// Note : utilise des connexions du pool comme les repositories
// (operations bloquantes via IBlockingExecutor).
// ─────────────────────────────────────────────────────────────
class MysqlIntrospector {
public:
    MysqlIntrospector(
        seastar::sharded<MysqlConnexionPool>& pool,
        std::shared_ptr<IBlockingExecutor> executor
        );

    // Verifie si la base de donnees existe (utilise INFORMATION_SCHEMA.SCHEMATA)
    seastar::future<bool> database_exists(const std::string& database_name);

    // Liste toutes les tables de la database courante
    seastar::future<std::vector<std::string>> list_tables();

    // Lit les colonnes d'une table
    seastar::future<std::vector<ColumnInfo>> list_columns(const std::string& table_name);

    // Lit les index d'une table
    seastar::future<std::vector<IndexInfo>> list_indexes(const std::string& table_name);

    // Lit les cles etrangeres d'une table
    seastar::future<std::vector<ForeignKeyInfo>> list_foreign_keys(const std::string& table_name);

    // Snapshot complet : appelle list_tables + list_columns + list_indexes + list_foreign_keys
    seastar::future<SchemaSnapshot> snapshot(const std::string& database_name);

private:
    seastar::sharded<MysqlConnexionPool>& _pool;
    std::shared_ptr<IBlockingExecutor> _executor;
};

} // namespace sea::infrastructure::persistence::mysql