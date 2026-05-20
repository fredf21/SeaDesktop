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
#include "persistence/mysql/sea_files_table.h"
#include "spdlog/spdlog.h"
#include <seastar/util/log.hh>

namespace sea::infrastructure::persistence::mysql {
//static seastar::logger sea_log("sea_backend");

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
/*    error,
    warn,
    info,
    debug,
    trace,*/
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
        spdlog::get("sea.persistence")->info(
            "create_database_if_missing=false, skip"
            );

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
    spdlog::get("sea.persistence")->info(
        "Checking database '{}' on {}", dbname, mysql_url
        );

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
                    spdlog::get("sea.persistence")->info(
                        "Database '{}' already exists", dbname
                        );

                    return true;
                }

                // Genere le SQL CREATE DATABASE
                const std::string sql =
                    MysqlSchemaGenerator::generate_create_database_sql(dbname);

                if (dry_run) {
                    spdlog::get("sea.persistence")->warn(
                        "[DRY RUN] {}", sql
                        );
                    return true;
                }

                spdlog::get("sea.persistence")->info(
                    "Creating database: {}", dbname
                    );
                auto stmt = std::unique_ptr<sql::Statement>(conn->createStatement());
                stmt->execute(sql);
                spdlog::get("sea.persistence")->info(
                    "Database '{}' created", dbname
                    );

                return true;
            } catch (const sql::SQLException& e) {
                spdlog::get("sea.persistence")->error(
                    "CREATE DATABASE error: {}", e.what()
                    );
                return false;
            } catch (const std::exception& e) {
                spdlog::get("sea.persistence")->error(
                    "Connection error: {}", e.what()
                    );
                return false;
            }
        }
        );

    co_return ok;
}

// ─────────────────────────────────────────────────────────────
// ensure_sea_files_table
//
// Crée la table système sea_files si elle n'existe pas. Doit être
// appelée APRÈS ensure_database_exists() (la DB doit exister et
// le pool doit être démarré) et AVANT compute_and_apply_diff()
// (les entités peuvent y référer via des FK).
//
// Idempotent grâce au IF NOT EXISTS.
// ─────────────────────────────────────────────────────────────
seastar::future<bool>
MysqlBootstrapper::ensure_sea_files_table()
{
    auto log = spdlog::get("sea.persistence");

    const auto sql = SeaFilesTable::generate_create_table_sql();
    log->info("Ensuring system table `{}` exists",
              std::string(SeaFilesTable::TABLE_NAME));

    if (_config.migrations.dry_run) {
        log->info("[dry-run] {}", sql);
        co_return true;
    }

    // Réutilise execute_sql() : passe par le pool comme tout
    // autre statement DDL. Idempotent côté SQL (IF NOT EXISTS).
    const bool ok = co_await execute_sql(sql);
    if (!ok) {
        log->error("Failed to create system table `{}`",
                   std::string(SeaFilesTable::TABLE_NAME));
    }
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
        spdlog::get("sea.persistence")->warn(
            "[DRY RUN] {}", sql
            );
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
                spdlog::get("sea.persistence")->error(
                    "SQL error: {} (SQL was: {})", e.what(), sql
                    );

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
                spdlog::get("sea.persistence")->error(
                    "SQL error (no db): {}", e.what()
                    );
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
    // ── 1. Tri topologique ────────────────────────────
    const auto sorted_entities = MysqlSchemaGenerator::topological_sort(_schema.entities);

    auto log = spdlog::get("sea.persistence");
    if (log->should_log(spdlog::level::debug)) {
        log->debug("Topological order:");
        for (const auto& entity : sorted_entities) {  // adapte au nom reel
            log->debug("  - {}", entity->name);
        }
    }

    const auto mode = _config.migrations.mode;

    // ── 2. Pour chaque entite : CREATE TABLE ou diff complet ────
    for (const auto* entity : sorted_entities) {
        const std::string table_name =
            !entity->table_name.empty() ? entity->table_name : entity->name;

        if (!snapshot.has_table(table_name)) {
            // Table manquante : CREATE TABLE
            spdlog::get("sea.persistence")->info(
                "CREATE TABLE: {}", table_name
                );
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
            spdlog::get("sea.persistence")->info(
                "{}: {}", table_name, diff.description
                );

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
                spdlog::get("sea.persistence")->warn(
                    "  SKIP: {}", skip_reason
                    );
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

        // ── 2a. Compute column diffs ────────────
        const auto column_diffs = SchemaDiffer::compute_column_diffs(*entity, *table_info);

        for (const auto& diff : column_diffs) {
            // Skip si le field a ete renomme (deja gere)
            if (SchemaDiffer::field_was_renamed(diff.column_name, rename_diffs)) {
                continue;
            }
            spdlog::get("sea.persistence")->info(
                "{}: {}", table_name, diff.description
                );

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
                    spdlog::get("sea.persistence")->warn(
                        "  SKIP: {}", skip_reason
                        );
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

        // ── 2b. Compute index diffs ─────────────────
        const auto index_diffs = SchemaDiffer::compute_index_diffs(*entity, *table_info);

        for (const auto& diff : index_diffs) {
            // Skip si le field a ete renomme (l'index sera recalcule au prochain boot)
            if (SchemaDiffer::field_was_renamed(diff.column_name, rename_diffs)) {
                continue;
            }
            // Skip si l'index pointe vers une colonne deja renommee
            if (SchemaDiffer::column_was_renamed_from(diff.column_name, rename_diffs)) {
                continue;
            }

            spdlog::get("sea.persistence")->info(
                "{}: {}", table_name, diff.description
                );
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
                spdlog::get("sea.persistence")->warn(
                    "  SKIP: {}", skip_reason
                    );
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

    // ── 3. Tables pivot M2M  ──
    std::set<std::string> created_pivots;

    for (const auto& entity : _schema.entities) {
        for (const auto& relation : entity.relations) {
            if (relation.kind != sea::domain::RelationKind::ManyToMany) continue;
            if (relation.pivot_table.empty()) continue;
            if (created_pivots.count(relation.pivot_table)) continue;
            created_pivots.insert(relation.pivot_table);

            if (snapshot.has_table(relation.pivot_table)) continue;

            spdlog::get("sea.persistence")->info(
                "CREATE PIVOT TABLE: {}", relation.pivot_table
                );

            const auto sql = MysqlSchemaGenerator::generate_pivot_table_sql(
                relation.pivot_table,
                relation.source_fk_column,
                relation.target_fk_column,
                sea::domain::Entity::to_route_plural(entity.name),
                sea::domain::Entity::to_route_plural(relation.target_entity),
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
        spdlog::get("sea.persistence")->info(
            "migrations.enabled=false, skip"
            );
        result.success = true;
        co_return result;
    }
    auto persist_log = spdlog::get("sea.persistence");
    persist_log->info("=== Starting bootstrap ===");
    persist_log->info("Mode: {}", to_string(_config.migrations.mode));
    persist_log->info("Dry run: {}", _config.migrations.dry_run ? "YES" : "no");


    // ── 1. Ensure database exists (avant le pool) ──────────────
    const bool db_ok = co_await ensure_database_exists();
    if (!db_ok) {
        result.errors.push_back("Failed to ensure database exists");
        result.success = false;
        co_return result;
    }

    // ── 1bis. Ensure system table sea_files exists (conditionnel) ─
    // Crée la table uniquement si le schema utilise au moins un champ
    // File. Sinon on skip pour eviter de polluer la DB avec une table
    // inutile.
    //
    // Doit être créée AVANT l'introspection pour qu'elle apparaisse
    // dans le snapshot (sinon le SchemaDiffer la verrait comme une
    // table orpheline en mode Aggressive). Et AVANT les tables
    // d'entités car celles-ci peuvent y référer via des FK.
    if (_schema.has_file_fields()) {
        const bool sea_files_ok = co_await ensure_sea_files_table();
        if (!sea_files_ok) {
            result.errors.push_back("Failed to ensure system table sea_files exists");
            result.success = false;
            co_return result;
        }
    } else {
        spdlog::get("sea.persistence")->info(
            "MysqlBootstrapper: schema has no File fields, skipping sea_files table creation");
    }

    // ── 2. Introspect (le pool est deja demarre a ce stade) ────
    MysqlIntrospector introspector(_pool, _executor);
    const auto snapshot = co_await introspector.snapshot(_config.database_name);

    persist_log->info(
        "Current schema: {} table(s) in '{}'",
        snapshot.tables.size(), _config.database_name
        );
    if (persist_log->should_log(spdlog::level::debug)) {
        for (const auto& [name, table] : snapshot.tables) {
            persist_log->debug("  - {} ({} cols)", name, table.columns.size());
        }
    }


    // ── 3. Compute diff and apply ──────────────────────────────
    co_await compute_and_apply_diff(snapshot, result);

    // ── 4. Resume ──────────────────────────────────────────────
    persist_log->info("=== Summary ===");

    persist_log->info("Tables created: {}", result.tables_created.size());
    for (const auto& t : result.tables_created) {
        persist_log->info("  + {}", t);
    }

    persist_log->info("Columns added: {}", result.columns_added.size());
    for (const auto& c : result.columns_added) {
        persist_log->info("  + {}", c);
    }

    persist_log->info("Columns renamed: {}", result.columns_renamed.size());
    for (const auto& r : result.columns_renamed) {
        persist_log->info("  ~ {}", r);
    }

    persist_log->info("Columns modified: {}", result.columns_modified.size());
    for (const auto& c : result.columns_modified) {
        persist_log->info("  ~ {}", c);
    }

    persist_log->info("Indexes changed: {}", result.indexes_changed.size());
    for (const auto& i : result.indexes_changed) {
        persist_log->info("  ~ {}", i);
    }

    persist_log->info("Pivots created: {}", result.pivots_created.size());
    for (const auto& p : result.pivots_created) {
        persist_log->info("  + {}", p);
    }

    // Errors -> niveau error (different)
    persist_log->info("Errors: {}", result.errors.size());
    for (const auto& e : result.errors) {
        persist_log->error("  ! {}", e);
    }


    result.success = result.errors.empty();
    if (result.success) {
        persist_log->info("=== SUCCESS ===");
    } else {
        persist_log->error("=== FAILED ===");
    }


    co_return result;
}

} // namespace sea::infrastructure::persistence::mysql