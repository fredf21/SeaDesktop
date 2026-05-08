#include "mysql_introspector.h"

#include <cppconn/exception.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>

#include <seastar/core/coroutine.hh>

#include <iostream>
#include <memory>
#include <utility>

namespace sea::infrastructure::persistence::mysql {

namespace {

/**
 * Helper : execute une operation MySQL bloquante hors du reactor Seastar.
 *
 * Pattern identique au run_blocking_mysql du repository, mais sans
 * support des transactions (l'introspecteur ne participe jamais a une txn).
 */
template <typename Result, typename Func>
seastar::future<Result> run_blocking_mysql(
    MysqlConnexionPool& pool,
    IBlockingExecutor& executor,
    Func&& func)
{
    auto* conn = co_await pool.acquire();
    try {
        auto result = co_await executor.submit(
            [conn, fn = std::forward<Func>(func)]() mutable -> Result {
                return fn(conn);
            }
            );
        pool.release(conn);
        co_return result;
    } catch (...) {
        pool.release(conn);
        throw;
    }
}

} // namespace anonyme

// ─────────────────────────────────────────────────────────────
// Constructeur
// ─────────────────────────────────────────────────────────────
MysqlIntrospector::MysqlIntrospector(
    seastar::sharded<MysqlConnexionPool>& pool,
    std::shared_ptr<IBlockingExecutor> executor)
    : _pool(pool)
    , _executor(std::move(executor))
{
}

// ─────────────────────────────────────────────────────────────
// database_exists
// ─────────────────────────────────────────────────────────────
seastar::future<bool>
MysqlIntrospector::database_exists(const std::string& database_name)
{
    auto& pool = _pool.local();
    co_return co_await run_blocking_mysql<bool>(
        pool,
        *_executor,
        [database_name](sql::Connection* conn) -> bool {
            try {
                auto stmt = std::unique_ptr<sql::PreparedStatement>(
                    conn->prepareStatement(
                        "SELECT COUNT(*) FROM INFORMATION_SCHEMA.SCHEMATA "
                        "WHERE SCHEMA_NAME = ?"
                        )
                    );
                stmt->setString(1, database_name);

                auto rs = std::unique_ptr<sql::ResultSet>(stmt->executeQuery());
                if (rs->next()) {
                    return rs->getInt(1) > 0;
                }
                return false;
            } catch (const sql::SQLException& e) {
                std::cerr << "[INTROSPECT] database_exists error: "
                          << e.what() << "\n";
                return false;
            }
        }
        );
}

// ─────────────────────────────────────────────────────────────
// list_tables
// ─────────────────────────────────────────────────────────────
seastar::future<std::vector<std::string>>
MysqlIntrospector::list_tables()
{
    auto& pool = _pool.local();
    co_return co_await run_blocking_mysql<std::vector<std::string>>(
        pool,
        *_executor,
        [](sql::Connection* conn) -> std::vector<std::string> {
            std::vector<std::string> tables;
            try {
                auto stmt = std::unique_ptr<sql::Statement>(conn->createStatement());
                auto rs = std::unique_ptr<sql::ResultSet>(
                    stmt->executeQuery("SHOW TABLES")
                    );
                while (rs->next()) {
                    tables.push_back(std::string(rs->getString(1)));
                }
            } catch (const sql::SQLException& e) {
                std::cerr << "[INTROSPECT] list_tables error: "
                          << e.what() << "\n";
            }
            return tables;
        }
        );
}

// ─────────────────────────────────────────────────────────────
// list_columns
// ─────────────────────────────────────────────────────────────
seastar::future<std::vector<ColumnInfo>>
MysqlIntrospector::list_columns(const std::string& table_name)
{
    auto& pool = _pool.local();
    co_return co_await run_blocking_mysql<std::vector<ColumnInfo>>(
        pool,
        *_executor,
        [table_name](sql::Connection* conn) -> std::vector<ColumnInfo> {
            std::vector<ColumnInfo> columns;
            try {
                auto stmt = std::unique_ptr<sql::PreparedStatement>(
                    conn->prepareStatement(
                        "SELECT "
                        "  COLUMN_NAME, "
                        "  DATA_TYPE, "
                        "  COLUMN_TYPE, "
                        "  IS_NULLABLE, "
                        "  COLUMN_KEY, "
                        "  EXTRA, "
                        "  COLUMN_DEFAULT "
                        "FROM INFORMATION_SCHEMA.COLUMNS "
                        "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = ? "
                        "ORDER BY ORDINAL_POSITION"
                        )
                    );
                stmt->setString(1, table_name);

                auto rs = std::unique_ptr<sql::ResultSet>(stmt->executeQuery());
                while (rs->next()) {
                    ColumnInfo info;
                    info.name = std::string(rs->getString("COLUMN_NAME"));
                    info.data_type = std::string(rs->getString("DATA_TYPE"));
                    info.column_type = std::string(rs->getString("COLUMN_TYPE"));
                    info.is_nullable = (std::string(rs->getString("IS_NULLABLE")) == "YES");

                    const std::string column_key(rs->getString("COLUMN_KEY"));
                    info.is_primary_key = (column_key == "PRI");
                    info.is_unique = (column_key == "UNI");

                    const std::string extra(rs->getString("EXTRA"));
                    info.is_auto_increment = (extra.find("auto_increment") != std::string::npos);

                    if (!rs->isNull("COLUMN_DEFAULT")) {
                        info.default_value = std::string(rs->getString("COLUMN_DEFAULT"));
                    }

                    columns.push_back(std::move(info));
                }
            } catch (const sql::SQLException& e) {
                std::cerr << "[INTROSPECT] list_columns(" << table_name << ") error: "
                          << e.what() << "\n";
            }
            return columns;
        }
        );
}

// ─────────────────────────────────────────────────────────────
// list_indexes
// ─────────────────────────────────────────────────────────────
seastar::future<std::vector<IndexInfo>>
MysqlIntrospector::list_indexes(const std::string& table_name)
{
    auto& pool = _pool.local();
    co_return co_await run_blocking_mysql<std::vector<IndexInfo>>(
        pool,
        *_executor,
        [table_name](sql::Connection* conn) -> std::vector<IndexInfo> {
            std::unordered_map<std::string, IndexInfo> indexes_map;
            try {
                auto stmt = std::unique_ptr<sql::PreparedStatement>(
                    conn->prepareStatement(
                        "SELECT "
                        "  INDEX_NAME, "
                        "  COLUMN_NAME, "
                        "  NON_UNIQUE, "
                        "  SEQ_IN_INDEX "
                        "FROM INFORMATION_SCHEMA.STATISTICS "
                        "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = ? "
                        "ORDER BY INDEX_NAME, SEQ_IN_INDEX"
                        )
                    );
                stmt->setString(1, table_name);

                auto rs = std::unique_ptr<sql::ResultSet>(stmt->executeQuery());
                while (rs->next()) {
                    const std::string idx_name(rs->getString("INDEX_NAME"));
                    const std::string col_name(rs->getString("COLUMN_NAME"));
                    const int non_unique = rs->getInt("NON_UNIQUE");

                    auto& info = indexes_map[idx_name];
                    if (info.name.empty()) {
                        info.name = idx_name;
                        info.is_unique = (non_unique == 0);
                        info.is_primary = (idx_name == "PRIMARY");
                    }
                    info.columns.push_back(col_name);
                }
            } catch (const sql::SQLException& e) {
                std::cerr << "[INTROSPECT] list_indexes(" << table_name << ") error: "
                          << e.what() << "\n";
            }

            std::vector<IndexInfo> result;
            result.reserve(indexes_map.size());
            for (auto& [_, info] : indexes_map) {
                result.push_back(std::move(info));
            }
            return result;
        }
        );
}

// ─────────────────────────────────────────────────────────────
// list_foreign_keys
// ─────────────────────────────────────────────────────────────
seastar::future<std::vector<ForeignKeyInfo>>
MysqlIntrospector::list_foreign_keys(const std::string& table_name)
{
    auto& pool = _pool.local();
    co_return co_await run_blocking_mysql<std::vector<ForeignKeyInfo>>(
        pool,
        *_executor,
        [table_name](sql::Connection* conn) -> std::vector<ForeignKeyInfo> {
            std::vector<ForeignKeyInfo> fks;
            try {
                auto stmt = std::unique_ptr<sql::PreparedStatement>(
                    conn->prepareStatement(
                        "SELECT "
                        "  kcu.CONSTRAINT_NAME, "
                        "  kcu.COLUMN_NAME, "
                        "  kcu.REFERENCED_TABLE_NAME, "
                        "  kcu.REFERENCED_COLUMN_NAME, "
                        "  rc.DELETE_RULE "
                        "FROM INFORMATION_SCHEMA.KEY_COLUMN_USAGE kcu "
                        "JOIN INFORMATION_SCHEMA.REFERENTIAL_CONSTRAINTS rc "
                        "  ON kcu.CONSTRAINT_NAME = rc.CONSTRAINT_NAME "
                        "  AND kcu.CONSTRAINT_SCHEMA = rc.CONSTRAINT_SCHEMA "
                        "WHERE kcu.TABLE_SCHEMA = DATABASE() "
                        "  AND kcu.TABLE_NAME = ? "
                        "  AND kcu.REFERENCED_TABLE_NAME IS NOT NULL"
                        )
                    );
                stmt->setString(1, table_name);

                auto rs = std::unique_ptr<sql::ResultSet>(stmt->executeQuery());
                while (rs->next()) {
                    ForeignKeyInfo fk;
                    fk.constraint_name = std::string(rs->getString("CONSTRAINT_NAME"));
                    fk.column_name = std::string(rs->getString("COLUMN_NAME"));
                    fk.referenced_table = std::string(rs->getString("REFERENCED_TABLE_NAME"));
                    fk.referenced_column = std::string(rs->getString("REFERENCED_COLUMN_NAME"));
                    fk.on_delete_action = std::string(rs->getString("DELETE_RULE"));
                    fks.push_back(std::move(fk));
                }
            } catch (const sql::SQLException& e) {
                std::cerr << "[INTROSPECT] list_foreign_keys(" << table_name << ") error: "
                          << e.what() << "\n";
            }
            return fks;
        }
        );
}

// ─────────────────────────────────────────────────────────────
// snapshot - lit TOUT le schema en une fois
// ─────────────────────────────────────────────────────────────
seastar::future<SchemaSnapshot>
MysqlIntrospector::snapshot(const std::string& database_name)
{
    SchemaSnapshot snap;
    snap.database_name = database_name;

    const auto tables = co_await list_tables();

    for (const auto& table_name : tables) {
        TableInfo info;
        info.name = table_name;
        info.columns = co_await list_columns(table_name);
        info.indexes = co_await list_indexes(table_name);
        info.foreign_keys = co_await list_foreign_keys(table_name);

        snap.tables.emplace(table_name, std::move(info));
    }

    co_return snap;
}

} // namespace sea::infrastructure::persistence::mysql