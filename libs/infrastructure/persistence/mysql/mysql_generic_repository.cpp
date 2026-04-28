#include "mysql_generic_repository.h"

#include <cppconn/connection.h>
#include <cppconn/exception.h>
#include <cppconn/metadata.h>
#include <cppconn/resultset.h>
#include <cppconn/resultset_metadata.h>
#include <cppconn/statement.h>

#include "persistence/utilities.h"

#include <seastar/core/coroutine.hh>

#include <memory>
#include <sstream>

namespace sea::infrastructure::persistence::mysql {

namespace {

runtime::DynamicValue read_typed_value(
    sql::ResultSet* rs,
    const std::string& field_name,
    sea::domain::FieldType field_type)
{
    if (rs->isNull(field_name)) {
        return std::monostate{};
    }

    using sea::domain::FieldType;

    switch (field_type) {
    case FieldType::String:
    case FieldType::Text:
    case FieldType::UUID:
    case FieldType::Password:
    case FieldType::Email:
    case FieldType::Timestamp:
        return std::string(rs->getString(field_name));

    case FieldType::Int:
        return static_cast<std::int64_t>(rs->getInt64(field_name));

    case FieldType::Float:
        return static_cast<double>(rs->getDouble(field_name));

    case FieldType::Bool:
        return rs->getBoolean(field_name);
    }

    return std::monostate{};
}

runtime::DynamicRecord resultset_to_record(
    sql::ResultSet* rs,
    const sea::domain::Entity& entity)
{
    runtime::DynamicRecord record;

    for (const auto& field : entity.fields) {
        record[field.name] = read_typed_value(rs, field.name, field.type);
    }

    return record;
}

void bindValue(
    sql::PreparedStatement* stmt,
    int index,
    const runtime::DynamicValue& value)
{
    if (std::holds_alternative<std::monostate>(value)) {
        stmt->setNull(index, 0);
    } else if (std::holds_alternative<std::string>(value)) {
        stmt->setString(index, std::get<std::string>(value));
    } else if (std::holds_alternative<std::int64_t>(value)) {
        stmt->setInt64(index, std::get<std::int64_t>(value));
    } else if (std::holds_alternative<double>(value)) {
        stmt->setDouble(index, std::get<double>(value));
    } else if (std::holds_alternative<bool>(value)) {
        stmt->setBoolean(index, std::get<bool>(value));
    } else {
        throw std::runtime_error("Impossible de binder un tableau sur une colonne SQL simple");
    }
}

const sea::domain::Field* find_field_by_name(
    const sea::domain::Entity& entity,
    const std::string& field_name)
{
    for (const auto& field : entity.fields) {
        if (field.name == field_name) {
            return &field;
        }
    }

    return nullptr;
}

const sea::domain::Field* find_id_field(const sea::domain::Entity& entity)
{
    return find_field_by_name(entity, "id");
}

std::optional<std::string> generate_mysql_uuid(sql::Connection* conn)
{
    try {
        auto stmt = std::unique_ptr<sql::Statement>(conn->createStatement());

        auto rs = std::unique_ptr<sql::ResultSet>(
            stmt->executeQuery("SELECT UUID()")
            );

        if (rs->next()) {
            return std::string(rs->getString(1));
        }

        return std::nullopt;

    } catch (const sql::SQLException&) {
        return std::nullopt;
    }
}

std::string build_select_columns(const sea::domain::Entity& entity)
{
    std::ostringstream sql;

    for (std::size_t i = 0; i < entity.fields.size(); ++i) {
        if (i > 0) {
            sql << ", ";
        }

        const auto& field = entity.fields[i];

        if (field.type == sea::domain::FieldType::UUID) {
            sql << "BIN_TO_UUID(`" << field.name << "`, 1) AS `" << field.name << "`";
        } else {
            sql << "`" << field.name << "`";
        }
    }

    return sql.str();
}

std::string build_id_where_clause(const sea::domain::Entity& entity)
{
    const auto* id_field = find_id_field(entity);

    if (id_field != nullptr &&
        id_field->type == sea::domain::FieldType::UUID) {
        return "`id` = UUID_TO_BIN(?, 1)";
    }

    return "`id` = ?";
}


/**
 * Helper critique : exécute une opération MySQL bloquante hors du reactor Seastar.
 *
 * Flow :
 * 1. Acquire connexion (non bloquant côté Seastar)
 * 2. Exécuter la requête dans le thread pool (bloquant OK)
 * 3. Retourner le résultat dans le reactor (via future)
 * 4. Release connexion
 *
 * Pourquoi ?
 * → éviter de bloquer le thread Seastar (sinon perte totale de perf)
 */
template <typename Result, typename Func>
seastar::future<Result> run_blocking_mysql(
    MysqlConnexionPool& pool,
    IBlockingExecutor& executor,
    Func&& func)
{
    std::cout << "RUN BLOCKING MYSQL\n";
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

} // namespace


/**
 * Constructeur du repository MySQL
 *
 * @param pool pool de connexions MySQL (géré par Seastar, shard-local)
 * @param schema_registry registre des entités runtime
 * @param executor thread pool pour exécuter les opérations bloquantes
 */
MySQLGenericRepository::MySQLGenericRepository(
    seastar::sharded<MysqlConnexionPool>& pool,
    std::shared_ptr<runtime::SchemaRuntimeRegistry> schema_registry,
    std::shared_ptr<IBlockingExecutor> executor)
    : _pool(pool)
    , _schema_registry(std::move(schema_registry))
    , _executor(std::move(executor))
{
}

seastar::future<std::optional<runtime::DynamicRecord>>
MySQLGenericRepository::create(
    const std::string& entity_name,
    runtime::DynamicRecord record)
{
    auto& pool = _pool.local();

    co_return co_await run_blocking_mysql<std::optional<runtime::DynamicRecord>>(
        pool,
        *_executor,
        [this, entity_name, record = std::move(record)](sql::Connection* conn) mutable
        -> std::optional<runtime::DynamicRecord> {
            using namespace sea::infrastructure::persistence::utilities;

            ValidationResult validation_result;

            const auto* entity =
                get_required_entity(*_schema_registry, entity_name, validation_result);

            if (entity == nullptr) {
                return std::nullopt;
            }

            const auto* id_field = find_id_field(*entity);

            const std::string table_name = resolve_table_name(*entity);
            if (!validate_sql_identifier(table_name, validation_result)) {
                return std::nullopt;
            }

            if (!validate_record_keys(*_schema_registry, entity_name, record, validation_result)) {
                return std::nullopt;
            }

            if (id_field != nullptr &&
                id_field->type == sea::domain::FieldType::UUID &&
                record.find("id") == record.end()) {
                auto generated_id = generate_mysql_uuid(conn);
                if (!generated_id.has_value()) {
                    return std::nullopt;
                }

                record["id"] = generated_id.value();
            }

            const bool should_fetch_auto_increment_id =
                id_field != nullptr &&
                id_field->type == sea::domain::FieldType::Int &&
                record.find("id") == record.end();

            const auto columns = collect_columns_in_schema_order(*entity, record);

            if (columns.empty()) {
                return std::nullopt;
            }

            try {
                std::ostringstream sql;
                sql << "INSERT INTO `" << table_name << "` (";

                for (std::size_t i = 0; i < columns.size(); ++i) {
                    if (i > 0) {
                        sql << ", ";
                    }
                    sql << "`" << columns[i] << "`";
                }

                sql << ") VALUES (";

                for (std::size_t i = 0; i < columns.size(); ++i) {
                    if (i > 0) {
                        sql << ", ";
                    }

                    const auto* field = find_field_by_name(*entity, columns[i]);

                    if (field != nullptr &&
                        field->type == sea::domain::FieldType::UUID) {
                        sql << "UUID_TO_BIN(?, 1)";
                    } else {
                        sql << "?";
                    }
                }

                sql << ")";

                auto stmt = std::unique_ptr<sql::PreparedStatement>(
                    conn->prepareStatement(sql.str())
                    );

                for (std::size_t i = 0; i < columns.size(); ++i) {
                    const auto* value =
                        get_required_value(record, columns[i], validation_result);

                    if (value == nullptr) {
                        return std::nullopt;
                    }

                    bindValue(stmt.get(), static_cast<int>(i + 1), *value);
                }

                stmt->execute();

                if (should_fetch_auto_increment_id) {
                    auto id_stmt =
                        std::unique_ptr<sql::Statement>(conn->createStatement());

                    auto rs = std::unique_ptr<sql::ResultSet>(
                        id_stmt->executeQuery("SELECT LAST_INSERT_ID()")
                        );

                    if (rs->next()) {
                        record["id"] = static_cast<std::int64_t>(rs->getInt64(1));
                    } else {
                        return std::nullopt;
                    }
                }

                return record;

            } catch (const sql::SQLException&) {
                return std::nullopt;
            }
        }
        );
}

/**
 * Récupère tous les enregistrements d'une entité
 *
 *  La requête SQL est exécutée dans le thread pool
 */
seastar::future<std::vector<runtime::DynamicRecord>>
MySQLGenericRepository::find_all(const std::string& entity_name)
{
    auto& pool = _pool.local();

    co_return co_await run_blocking_mysql<std::vector<runtime::DynamicRecord>>(
        pool,
        *_executor,
        [this, entity_name](sql::Connection* conn)
        -> std::vector<runtime::DynamicRecord> {
            using namespace sea::infrastructure::persistence::utilities;

            ValidationResult validation_result;

            const auto* entity =
                get_required_entity(*_schema_registry, entity_name, validation_result);

            if (entity == nullptr) {
                return {};
            }

            const std::string table_name = resolve_table_name(*entity);

            if (!validate_sql_identifier(table_name, validation_result)) {
                return {};
            }

            try {
                std::ostringstream sql;
                sql << "SELECT " << build_select_columns(*entity)
                    << " FROM `" << table_name << "`";

                auto stmt =
                    std::unique_ptr<sql::Statement>(conn->createStatement());

                auto rs =
                    std::unique_ptr<sql::ResultSet>(stmt->executeQuery(sql.str()));

                std::vector<runtime::DynamicRecord> results;

                while (rs->next()) {
                    results.push_back(resultset_to_record(rs.get(), *entity));
                }

                return results;

            } catch (const sql::SQLException&) {
                return {};
            }
        }
        );
}

seastar::future<std::optional<runtime::DynamicRecord>>
MySQLGenericRepository::find_by_id(
    const std::string& entity_name,
    const std::string& id)
{
    auto& pool = _pool.local();

    co_return co_await run_blocking_mysql<std::optional<runtime::DynamicRecord>>(
        pool,
        *_executor,
        [this, entity_name, id](sql::Connection* conn)
        -> std::optional<runtime::DynamicRecord> {
            using namespace sea::infrastructure::persistence::utilities;

            ValidationResult validation_result;

            const auto* entity =
                get_required_entity(*_schema_registry, entity_name, validation_result);

            if (entity == nullptr) {
                return std::nullopt;
            }

            const std::string table_name = resolve_table_name(*entity);

            if (!validate_sql_identifier(table_name, validation_result)) {
                return std::nullopt;
            }

            try {
                std::ostringstream sql;
                sql << "SELECT " << build_select_columns(*entity)
                    << " FROM `" << table_name << "` WHERE "
                    << build_id_where_clause(*entity)
                    << " LIMIT 1";

                auto stmt = std::unique_ptr<sql::PreparedStatement>(
                    conn->prepareStatement(sql.str())
                    );

                stmt->setString(1, id);

                auto rs = std::unique_ptr<sql::ResultSet>(
                    stmt->executeQuery()
                    );

                if (!rs->next()) {
                    return std::nullopt;
                }

                return resultset_to_record(rs.get(), *entity);

            } catch (const sql::SQLException&) {
                return std::nullopt;
            }
        }
        );
}

seastar::future<std::optional<runtime::DynamicRecord>>
MySQLGenericRepository::find_one_by_field(
    const std::string& entity_name,
    const std::string& field_name,
    const std::string& value)
{
    auto& pool = _pool.local();

    co_return co_await run_blocking_mysql<std::optional<runtime::DynamicRecord>>(
        pool,
        *_executor,
        [this, entity_name, field_name, value](sql::Connection* conn)
        -> std::optional<runtime::DynamicRecord> {
            using namespace sea::infrastructure::persistence::utilities;

            ValidationResult validation_result;

            const auto* entity =
                get_required_entity(*_schema_registry, entity_name, validation_result);

            if (entity == nullptr) {
                return std::nullopt;
            }

            const auto* field = find_field_by_name(*entity, field_name);
            if (field == nullptr) {
                return std::nullopt;
            }

            const std::string table_name = resolve_table_name(*entity);

            if (!validate_sql_identifier(table_name, validation_result) ||
                !validate_sql_identifier(field_name, validation_result)) {
                return std::nullopt;
            }

            try {
                std::ostringstream sql;
                sql << "SELECT " << build_select_columns(*entity)
                    << " FROM `" << table_name << "` WHERE ";

                if (field->type == sea::domain::FieldType::UUID) {
                    sql << "`" << field_name << "` = UUID_TO_BIN(?, 1)";
                } else {
                    sql << "`" << field_name << "` = ?";
                }

                sql << " LIMIT 1";

                auto stmt = std::unique_ptr<sql::PreparedStatement>(
                    conn->prepareStatement(sql.str())
                    );

                stmt->setString(1, value);

                auto rs = std::unique_ptr<sql::ResultSet>(
                    stmt->executeQuery()
                    );

                if (!rs->next()) {
                    return std::nullopt;
                }

                return resultset_to_record(rs.get(), *entity);

            } catch (const sql::SQLException&) {
                return std::nullopt;
            }
        }
        );
}

seastar::future<bool>
MySQLGenericRepository::remove(
    const std::string& entity_name,
    const std::string& id)
{
    auto& pool = _pool.local();

    co_return co_await run_blocking_mysql<bool>(
        pool,
        *_executor,
        [this, entity_name, id](sql::Connection* conn) -> bool {
            using namespace sea::infrastructure::persistence::utilities;

            ValidationResult validation_result;

            const auto* entity =
                get_required_entity(*_schema_registry, entity_name, validation_result);

            if (entity == nullptr) {
                return false;
            }

            const std::string table_name = resolve_table_name(*entity);

            if (!validate_sql_identifier(table_name, validation_result)) {
                return false;
            }

            try {
                std::ostringstream sql;
                sql << "DELETE FROM `" << table_name << "` WHERE "
                    << build_id_where_clause(*entity);

                auto stmt = std::unique_ptr<sql::PreparedStatement>(
                    conn->prepareStatement(sql.str())
                    );

                stmt->setString(1, id);

                const int affected_rows = stmt->executeUpdate();
                return affected_rows > 0;

            } catch (const sql::SQLException&) {
                return false;
            }
        }
        );
}

seastar::future<sea::infrastructure::persistence::UpdateResponse>
MySQLGenericRepository::update(
    const std::string& entity_name,
    const std::string& id,
    runtime::DynamicRecord record)
{
    auto& pool = _pool.local();

    co_return co_await run_blocking_mysql<sea::infrastructure::persistence::UpdateResponse>(
        pool,
        *_executor,
        [this, entity_name, id, record = std::move(record)](sql::Connection* conn) mutable
        -> sea::infrastructure::persistence::UpdateResponse {
            using namespace sea::infrastructure::persistence::utilities;

            ValidationResult validation_result;

            const auto* entity =
                get_required_entity(*_schema_registry, entity_name, validation_result);

            if (entity == nullptr) {
                return {.status = false, .record = {}};
            }

            const std::string table_name = resolve_table_name(*entity);

            if (!validate_sql_identifier(table_name, validation_result)) {
                return {.status = false, .record = {}};
            }

            if (!validate_record_keys(*_schema_registry, entity_name, record, validation_result)) {
                return {.status = false, .record = {}};
            }

            const auto columns = collect_columns_in_schema_order(*entity, record, true);

            if (columns.empty()) {
                return {.status = false, .record = {}};
            }

            try {
                std::ostringstream sql;
                sql << "UPDATE `" << table_name << "` SET ";

                for (std::size_t i = 0; i < columns.size(); ++i) {
                    if (i > 0) {
                        sql << ", ";
                    }

                    const auto* field = find_field_by_name(*entity, columns[i]);

                    if (field != nullptr &&
                        field->type == sea::domain::FieldType::UUID) {
                        sql << "`" << columns[i] << "` = UUID_TO_BIN(?, 1)";
                    } else {
                        sql << "`" << columns[i] << "` = ?";
                    }
                }

                sql << " WHERE " << build_id_where_clause(*entity);

                auto stmt = std::unique_ptr<sql::PreparedStatement>(
                    conn->prepareStatement(sql.str())
                    );

                int param_index = 1;

                for (const auto& column : columns) {
                    const auto* value =
                        get_required_value(record, column, validation_result);

                    if (value == nullptr) {
                        return {.status = false, .record = {}};
                    }

                    bindValue(stmt.get(), param_index++, *value);
                }

                stmt->setString(param_index, id);

                const int affected_rows = stmt->executeUpdate();

                if (affected_rows <= 0) {
                    return {.status = false, .record = {}};
                }

                std::ostringstream select_sql;
                select_sql << "SELECT " << build_select_columns(*entity)
                           << " FROM `" << table_name << "` WHERE "
                           << build_id_where_clause(*entity)
                           << " LIMIT 1";

                auto select_stmt = std::unique_ptr<sql::PreparedStatement>(
                    conn->prepareStatement(select_sql.str())
                    );

                select_stmt->setString(1, id);

                auto rs = std::unique_ptr<sql::ResultSet>(
                    select_stmt->executeQuery()
                    );

                if (rs->next()) {
                    return UpdateResponse{
                        .status = true,
                        .record = resultset_to_record(rs.get(), *entity)
                    };
                }

                return {.status = false, .record = {}};

            } catch (const sql::SQLException&) {
                return {.status = false, .record = {}};
            }
        }
        );
}

seastar::future<bool>
MySQLGenericRepository::insert_pivot(
    const std::string& pivot_table,
    runtime::DynamicRecord values)
{
    using namespace sea::infrastructure::persistence::utilities;

    if (values.empty()) {
        throw std::runtime_error("insert_pivot: aucune valeur fournie");
    }

    ValidationResult validation_result;

    if (!validate_sql_identifier(pivot_table, validation_result)) {
        throw std::runtime_error("insert_pivot: nom de table pivot invalide");
    }

    auto& pool = _pool.local();

    co_return co_await run_blocking_mysql<bool>(
        pool,
        *_executor,
        [pivot_table, values = std::move(values)](sql::Connection* conn) mutable -> bool {
            try {
                std::vector<std::string> columns;
                std::vector<runtime::DynamicValue> bind_values;

                columns.reserve(values.size());
                bind_values.reserve(values.size());

                for (auto& [key, value] : values) {
                    columns.push_back(key);
                    bind_values.push_back(value);
                }

                std::ostringstream query;
                query << "INSERT INTO `" << pivot_table << "` (";

                for (std::size_t i = 0; i < columns.size(); ++i) {
                    if (i > 0) {
                        query << ", ";
                    }

                    query << "`" << columns[i] << "`";
                }

                query << ") VALUES (";

                for (std::size_t i = 0; i < columns.size(); ++i) {
                    if (i > 0) {
                        query << ", ";
                    }

                    query << "?";
                }

                query << ")";

                auto stmt = std::unique_ptr<sql::PreparedStatement>(
                    conn->prepareStatement(query.str())
                    );

                for (std::size_t i = 0; i < bind_values.size(); ++i) {
                    bindValue(stmt.get(), static_cast<int>(i + 1), bind_values[i]);
                }

                stmt->execute();
                return true;

            } catch (const sql::SQLException&) {
                return false;
            }
        }
        );
}

} // namespace sea::infrastructure::persistence::mysql