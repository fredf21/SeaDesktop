#include "mysql_bootstrapper.h"

#include <cppconn/driver.h>
#include <cppconn/connection.h>
#include <cppconn/exception.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/statement.h>

#include <seastar/core/coroutine.hh>

#include <iostream>
#include <memory>
#include <utility>
#include "mysql_schema_generator.h"
#include "persistence/mysql/schema_differ.h"

namespace sea::infrastructure::persistence::mysql {

namespace {

/**
 * Helper : execute un SQL statement bloquant via le pool + executor.
 * Pattern identique au repository.
 */
template <typename Func>
seastar::future<bool> run_blocking_sql(
    MysqlConnexionPool& pool,
    IBlockingExecutor& executor,
    Func&& func)
{
    auto* conn = co_await pool.acquire();
    try {
        const bool result = co_await executor.submit(
            [conn, fn = std::forward<Func>(func)]() mutable -> bool {
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
MysqlBootstrapper::MysqlBootstrapper(
    const sea::domain::DatabaseConfig& config,
    const sea::domain::Schema& schema,
    seastar::sharded<MysqlConnexionPool>& pool,
    std::shared_ptr<IBlockingExecutor> executor)
    : _config(config)
    , _schema(schema)
    , _pool(pool)
    , _executor(std::move(executor))
{
}

// ─────────────────────────────────────────────────────────────
// ensure_database_exists
//
// IMPORTANT : cette methode se connecte SANS specifier de database
// (juste host:port + user/pass). Elle est appelee AVANT que le pool
// ne soit demarre. Donc elle cree sa propre connexion ad-hoc.
// ─────────────────────────────────────────────────────────────
seastar::future<bool>
MysqlBootstrapper::ensure_database_exists()
{
    if (!_config.migrations.create_database_if_missing) {
        std::cerr << "[BOOTSTRAP] create_database_if_missing=false, skip\n";
        co_return true;
    }

    const std::string dbname = _config.database_name;
    const std::string host = _config.host;
    const std::string user = _config.username;
    const std::string pass = _config.password;
    const int port = _config.port;
    const bool dry_run = _config.migrations.dry_run;

    std::ostringstream url;
    url << "tcp://" << host << ":" << port;
    const std::string mysql_url = url.str();

    std::cerr << "[BOOTSTRAP] Checking database '" << dbname << "' on " << mysql_url << "\n";

    const bool ok = co_await _executor->submit(
        [mysql_url, user, pass, dbname, dry_run]() -> bool {
            try {
                auto* driver = sql::mysql::get_mysql_driver_instance();
                auto conn = std::unique_ptr<sql::Connection>(
                    driver->connect(mysql_url, user, pass)
                    );

                // Verifie si la DB existe
                auto check_stmt = std::unique_ptr<sql::PreparedStatement>(
                    conn->prepareStatement(
                        "SELECT COUNT(*) FROM INFORMATION_SCHEMA.SCHEMATA "
                        "WHERE SCHEMA_NAME = ?"
                        )
                    );
                check_stmt->setString(1, dbname);

                auto rs = std::unique_ptr<sql::ResultSet>(check_stmt->executeQuery());
                bool exists = false;
                if (rs->next()) {
                    exists = rs->getInt(1) > 0;
                }

                if (exists) {
                    std::cerr << "[BOOTSTRAP] Database '" << dbname << "' already exists\n";
                    return true;
                }

                // Genere le SQL CREATE DATABASE
                const std::string sql =
                    MysqlSchemaGenerator::generate_create_database_sql(dbname);

                if (dry_run) {
                    std::cerr << "[BOOTSTRAP][DRY RUN] " << sql << "\n";
                    return true;
                }

                std::cerr << "[BOOTSTRAP] Creating database: " << dbname << "\n";
                auto stmt = std::unique_ptr<sql::Statement>(conn->createStatement());
                stmt->execute(sql);
                std::cerr << "[BOOTSTRAP] Database '" << dbname << "' created\n";

                return true;
            } catch (const sql::SQLException& e) {
                std::cerr << "[BOOTSTRAP] CREATE DATABASE error: " << e.what() << "\n";
                return false;
            } catch (const std::exception& e) {
                std::cerr << "[BOOTSTRAP] Connection error: " << e.what() << "\n";
                return false;
            }
        }
        );

    co_return ok;
}

// ─────────────────────────────────────────────────────────────
// execute_sql - utilise le pool (DB doit exister)
// ─────────────────────────────────────────────────────────────
seastar::future<bool>
MysqlBootstrapper::execute_sql(const std::string& sql)
{
    const bool dry_run = _config.migrations.dry_run;

    if (dry_run) {
        std::cerr << "[BOOTSTRAP][DRY RUN] " << sql << "\n";
        co_return true;
    }

    auto& pool = _pool.local();
    co_return co_await run_blocking_sql(
        pool,
        *_executor,
        [sql](sql::Connection* conn) -> bool {
            try {
                auto stmt = std::unique_ptr<sql::Statement>(conn->createStatement());
                stmt->execute(sql);
                return true;
            } catch (const sql::SQLException& e) {
                std::cerr << "[BOOTSTRAP] SQL error: " << e.what() << "\n";
                std::cerr << "[BOOTSTRAP] SQL was: " << sql << "\n";
                return false;
            }
        }
        );
}

// ─────────────────────────────────────────────────────────────
// execute_sql_without_database (helper pour CREATE DATABASE)
// ─────────────────────────────────────────────────────────────
seastar::future<bool>
MysqlBootstrapper::execute_sql_without_database(const std::string& sql)
{
    // Cette methode est en fait integree dans ensure_database_exists().
    // On la garde pour usage futur eventuel.
    const std::string host = _config.host;
    const std::string user = _config.username;
    const std::string pass = _config.password;
    const int port = _config.port;

    std::ostringstream url;
    url << "tcp://" << host << ":" << port;
    const std::string mysql_url = url.str();

    co_return co_await _executor->submit(
        [mysql_url, user, pass, sql]() -> bool {
            try {
                auto* driver = sql::mysql::get_mysql_driver_instance();
                auto conn = std::unique_ptr<sql::Connection>(
                    driver->connect(mysql_url, user, pass)
                    );
                auto stmt = std::unique_ptr<sql::Statement>(conn->createStatement());
                stmt->execute(sql);
                return true;
            } catch (const sql::SQLException& e) {
                std::cerr << "[BOOTSTRAP] SQL error (no db): " << e.what() << "\n";
                return false;
            }
        }
        );
}

// ─────────────────────────────────────────────────────────────
// compute_and_apply_diff
// ─────────────────────────────────────────────────────────────
//
// ORDRE D'APPLICATION (critique) :
// 1. Renames (CHANGE COLUMN) - en premier pour que les autres diffs
//    voient le NOUVEAU schema apres rename
// 2. Column diffs (ADD/MODIFY)
// 3. Index diffs (ADD/DROP INDEX/UNIQUE)
// 4. Pivot tables M2M
// ═══════════════════════════════════════════════════════════════════════


seastar::future<>
MysqlBootstrapper::compute_and_apply_diff(
    const SchemaSnapshot& snapshot,
    BootstrapResult& result)
{
    // ── 1. Tri topologique (Phase A) ────────────────────────────
    const auto sorted_entities = MysqlSchemaGenerator::topological_sort(_schema.entities);

    std::cerr << "[BOOTSTRAP] Topological order:\n";
    for (const auto* entity : sorted_entities) {
        std::cerr << "  - " << entity->name << "\n";
    }

    const auto mode = _config.migrations.mode;

    // ── 2. Pour chaque entite : CREATE TABLE ou diff complet ────
    for (const auto* entity : sorted_entities) {
        const std::string table_name =
            !entity->table_name.empty() ? entity->table_name : entity->name;

        if (!snapshot.has_table(table_name)) {
            // Table manquante : CREATE TABLE (Phase A)
            std::cerr << "[BOOTSTRAP] CREATE TABLE: " << table_name << "\n";
            const auto sql = MysqlSchemaGenerator::generate_create_table_sql(*entity);
            const bool ok = co_await execute_sql(sql);
            if (ok) {
                result.tables_created.push_back(table_name);
            } else {
                result.errors.push_back("Failed to create table " + table_name);
            }
            continue;
        }

        const auto* table_info = snapshot.find_table(table_name);

        // ════════════════════════════════════════════════════════
        // RENAMES EN PREMIER
        // ════════════════════════════════════════════════════════
        const auto rename_diffs = SchemaDiffer::compute_renames(*entity, *table_info);

        for (const auto& diff : rename_diffs) {
            std::cerr << "[BOOTSTRAP] " << table_name << ": " << diff.description << "\n";

            // Decision selon le mode et le type de rename :
            // - Score = 100 (annotation explicite) : applique en modified+aggressive
            // - Score < 100 (heuristique) : applique uniquement en aggressive
            bool should_apply = false;
            std::string skip_reason;

            const bool is_explicit = (diff.rename_confidence_score == 100);

            if (mode == sea::domain::MigrationMode::Conservative) {
                skip_reason = "conservative mode (use 'modified' or 'aggressive' to apply)";
            } else if (mode == sea::domain::MigrationMode::Modified) {
                if (is_explicit) {
                    should_apply = true;  // annotation explicite est safe
                } else {
                    skip_reason = "heuristic rename in modified mode (use 'aggressive')";
                }
            } else if (mode == sea::domain::MigrationMode::Aggressive) {
                should_apply = true;  // tout est applique en aggressive
            }

            if (!should_apply) {
                std::cerr << "[BOOTSTRAP]   SKIP: " << skip_reason << "\n";
                result.warnings.push_back(diff.description + " skipped: " + skip_reason);
                continue;
            }

            // Applique le RENAME
            const auto sql = MysqlSchemaGenerator::generate_rename_column_sql(
                *entity, diff.previous_name, *diff.target_field
                );

            const bool ok = co_await execute_sql(sql);
            if (ok) {
                std::ostringstream entry;
                entry << table_name << "." << diff.previous_name
                      << " → " << table_name << "." << diff.column_name
                      << " (" << (is_explicit ? "explicit" : "heuristic")
                      << " score=" << diff.rename_confidence_score << ")";
                result.columns_renamed.push_back(entry.str());
            } else {
                result.errors.push_back(
                    "Failed to rename column " + diff.previous_name +
                    " → " + diff.column_name + " in " + table_name
                    );
            }
        }

        // Re-introspecter la table apres les renames pour avoir l'etat a jour ?
        // Pour V1 : on utilise le snapshot original mais on saute les fields/colonnes
        // deja gerees par les renames. Plus simple et evite un round-trip MySQL.

        // ── 2a. Compute column diffs (Phase A + B.1) ────────────
        const auto column_diffs = SchemaDiffer::compute_column_diffs(*entity, *table_info);

        for (const auto& diff : column_diffs) {
            // ✨ Skip si le field a ete renomme (deja gere)
            if (SchemaDiffer::field_was_renamed(diff.column_name, rename_diffs)) {
                continue;
            }

            std::cerr << "[BOOTSTRAP] " << table_name << ": " << diff.description << "\n";

            switch (diff.kind) {
            case ColumnDiffKind::Added: {
                const auto sql = MysqlSchemaGenerator::generate_add_column_sql(
                    *entity, *diff.target_field
                    );
                const bool ok = co_await execute_sql(sql);
                if (ok) {
                    result.columns_added.push_back(table_name + "." + diff.column_name);
                } else {
                    result.errors.push_back(
                        "Failed to add column " + table_name + "." + diff.column_name
                        );
                }
                break;
            }

            case ColumnDiffKind::TypeChanged:
            case ColumnDiffKind::NullabilityChanged:
            case ColumnDiffKind::DefaultChanged: {
                bool should_apply = false;
                std::string skip_reason;

                if (mode == sea::domain::MigrationMode::Conservative) {
                    skip_reason = "conservative mode (use 'modified' or 'aggressive' to apply)";
                } else if (mode == sea::domain::MigrationMode::Modified) {
                    if (diff.is_safe) {
                        should_apply = true;
                    } else {
                        skip_reason = "unsafe change in modified mode (use 'aggressive')";
                    }
                } else if (mode == sea::domain::MigrationMode::Aggressive) {
                    should_apply = true;
                }

                if (!should_apply) {
                    std::cerr << "[BOOTSTRAP]   SKIP: " << skip_reason << "\n";
                    result.warnings.push_back(diff.description + " skipped: " + skip_reason);
                    break;
                }

                const auto sql = MysqlSchemaGenerator::generate_modify_column_sql(
                    *entity, *diff.target_field
                    );
                const bool ok = co_await execute_sql(sql);
                if (ok) {
                    result.columns_modified.push_back(
                        table_name + "." + diff.column_name + " (" +
                        std::string(to_string(diff.kind)) + ")"
                        );
                } else {
                    result.errors.push_back(
                        "Failed to modify column " + table_name + "." + diff.column_name
                        );
                }
                break;
            }

            case ColumnDiffKind::IndexAdded:
            case ColumnDiffKind::IndexRemoved:
            case ColumnDiffKind::UniqueAdded:
            case ColumnDiffKind::UniqueRemoved:
            case ColumnDiffKind::Renamed:
                break;
            }
        }

        // ── 2b. Compute index diffs (Phase B.2) ─────────────────
        const auto index_diffs = SchemaDiffer::compute_index_diffs(*entity, *table_info);

        for (const auto& diff : index_diffs) {
            // ✨ Skip si le field a ete renomme (l'index sera recalcule au prochain boot)
            if (SchemaDiffer::field_was_renamed(diff.column_name, rename_diffs)) {
                continue;
            }
            // ✨ Skip si l'index pointe vers une colonne deja renommee
            if (SchemaDiffer::column_was_renamed_from(diff.column_name, rename_diffs)) {
                continue;
            }

            std::cerr << "[BOOTSTRAP] " << table_name << ": " << diff.description << "\n";

            bool should_apply = false;
            std::string skip_reason;

            if (mode == sea::domain::MigrationMode::Conservative) {
                skip_reason = "conservative mode (use 'modified' or 'aggressive' to apply)";
            } else if (mode == sea::domain::MigrationMode::Modified) {
                if (diff.is_safe) {
                    should_apply = true;
                } else {
                    skip_reason = "unsafe change in modified mode (use 'aggressive')";
                }
            } else if (mode == sea::domain::MigrationMode::Aggressive) {
                should_apply = true;
            }

            if (!should_apply) {
                std::cerr << "[BOOTSTRAP]   SKIP: " << skip_reason << "\n";
                result.warnings.push_back(diff.description + " skipped: " + skip_reason);
                continue;
            }

            std::string sql;
            switch (diff.kind) {
            case ColumnDiffKind::IndexAdded:
                sql = MysqlSchemaGenerator::generate_add_index_sql(table_name, diff.column_name);
                break;
            case ColumnDiffKind::IndexRemoved:
                sql = MysqlSchemaGenerator::generate_drop_index_sql(table_name, diff.index_name_to_drop);
                break;
            case ColumnDiffKind::UniqueAdded:
                sql = MysqlSchemaGenerator::generate_add_unique_sql(table_name, diff.column_name);
                break;
            case ColumnDiffKind::UniqueRemoved:
                sql = MysqlSchemaGenerator::generate_drop_unique_sql(table_name, diff.index_name_to_drop);
                break;
            default:
                continue;
            }

            const bool ok = co_await execute_sql(sql);
            if (ok) {
                result.indexes_changed.push_back(
                    table_name + "." + diff.column_name + " (" +
                    std::string(to_string(diff.kind)) + ")"
                    );
            } else {
                result.errors.push_back(
                    "Failed to apply index change on " + table_name + "." + diff.column_name
                    );
            }
        }
    }

    // ── 3. Tables pivot M2M (Phase A, code existant inchange) ──
    std::set<std::string> created_pivots;

    for (const auto& entity : _schema.entities) {
        for (const auto& relation : entity.relations) {
            if (relation.kind != sea::domain::RelationKind::ManyToMany) continue;
            if (relation.pivot_table.empty()) continue;
            if (created_pivots.count(relation.pivot_table)) continue;
            created_pivots.insert(relation.pivot_table);

            if (snapshot.has_table(relation.pivot_table)) continue;

            std::cerr << "[BOOTSTRAP] CREATE PIVOT TABLE: " << relation.pivot_table << "\n";

            const auto sql = MysqlSchemaGenerator::generate_pivot_table_sql(
                relation.pivot_table,
                relation.source_fk_column,
                relation.target_fk_column,
                entity.name,
                relation.target_entity,
                relation.on_delete
                );

            const bool ok = co_await execute_sql(sql);
            if (ok) {
                result.pivots_created.push_back(relation.pivot_table);
            } else {
                result.errors.push_back(
                    "Failed to create pivot table " + relation.pivot_table
                    );
            }
        }
    }

    co_return;
}




// ─────────────────────────────────────────────────────────────
// bootstrap (methode principale)
// ─────────────────────────────────────────────────────────────
seastar::future<BootstrapResult>
MysqlBootstrapper::bootstrap()
{
    BootstrapResult result;

    if (!_config.migrations.enabled) {
        std::cerr << "[BOOTSTRAP] migrations.enabled=false, skip\n";
        result.success = true;
        co_return result;
    }

    std::cerr << "[BOOTSTRAP] ─── Starting bootstrap ───\n";
    std::cerr << "[BOOTSTRAP] Mode: " << to_string(_config.migrations.mode) << "\n";
    std::cerr << "[BOOTSTRAP] Dry run: "
              << (_config.migrations.dry_run ? "YES" : "no") << "\n";

    // ── 1. Ensure database exists (avant le pool) ──────────────
    const bool db_ok = co_await ensure_database_exists();
    if (!db_ok) {
        result.errors.push_back("Failed to ensure database exists");
        result.success = false;
        co_return result;
    }

    // ── 2. Introspect (le pool est deja demarre a ce stade) ────
    MysqlIntrospector introspector(_pool, _executor);
    const auto snapshot = co_await introspector.snapshot(_config.database_name);

    std::cerr << "[BOOTSTRAP] Current schema: "
              << snapshot.tables.size() << " table(s) in '"
              << _config.database_name << "'\n";
    for (const auto& [name, table] : snapshot.tables) {
        std::cerr << "  - " << name << " (" << table.columns.size() << " cols)\n";
    }

    // ── 3. Compute diff and apply ──────────────────────────────
    co_await compute_and_apply_diff(snapshot, result);

    // ── 4. Resume ──────────────────────────────────────────────
    std::cerr << "[BOOTSTRAP] ─── Summary ───\n";
    std::cerr << "[BOOTSTRAP] Tables created: " << result.tables_created.size() << "\n";
    for (const auto& t : result.tables_created) {
        std::cerr << "  + " << t << "\n";
    }
    std::cerr << "[BOOTSTRAP] Columns added: " << result.columns_added.size() << "\n";
    for (const auto& c : result.columns_added) {
        std::cerr << "  + " << c << "\n";
    }
    std::cerr << "[BOOTSTRAP] Columns renamed: " << result.columns_renamed.size() << "\n";
    for (const auto& r : result.columns_renamed) {
        std::cerr << "  ↻ " << r << "\n";
    }

    std::cerr << "[BOOTSTRAP] Columns modified: " << result.columns_modified.size() << "\n";
    for (const auto& c : result.columns_modified) {
        std::cerr << "  ~ " << c << "\n";
    }

    std::cerr << "[BOOTSTRAP] Indexes changed: " << result.indexes_changed.size() << "\n";
    for (const auto& i : result.indexes_changed) {
        std::cerr << "  ~ " << i << "\n";
    }
    std::cerr << "[BOOTSTRAP] Pivots created: " << result.pivots_created.size() << "\n";
    for (const auto& p : result.pivots_created) {
        std::cerr << "  + " << p << "\n";
    }
    std::cerr << "[BOOTSTRAP] Errors: " << result.errors.size() << "\n";
    for (const auto& e : result.errors) {
        std::cerr << "  ! " << e << "\n";
    }

    result.success = result.errors.empty();
    std::cerr << "[BOOTSTRAP] ─── "
              << (result.success ? "SUCCESS" : "FAILED")
              << " ───\n";

    co_return result;
}

} // namespace sea::infrastructure::persistence::mysql