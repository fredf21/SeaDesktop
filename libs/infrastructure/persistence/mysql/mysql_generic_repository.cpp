#include "mysql_generic_repository.h"
#include "database_mappings/mysql_type_mapping.h"
#include "exception_handling.h"
#include "persistence/utilities.h"
#include "spdlog/spdlog.h"
#include <seastar/core/coroutine.hh>
#include <memory>
#include <sstream>
#include <utility>

namespace sea::infrastructure::persistence::mysql {

namespace {
/**
 * Determine si une sql::SQLException indique que la connexion
 * MySQL sous-jacente est devenue inutilisable.
 *
 * Codes detectes :
 *   2006 = MySQL server has gone away
 *   2013 = Lost connection to MySQL server during query
 *   2003 = Can't connect to MySQL server
 *   2055 = Lost connection during query (system error)
 *   2026 = SSL connection error
 *
 * SQL state 08xxx = Connection exception (ISO/IEC 9075).
 *
 * Quand cette fonction retourne true, le caller DOIT re-throw
 * l'exception pour que run_blocking_mysql puisse appeler
 * discard_and_replace sur la connexion. Si on swallow l'exception
 * et qu'on retourne nullopt, la connexion morte sera rebalancee
 * dans le pool et provoquera un SEGFAULT au prochain usage.
 */

inline bool is_connection_dead_exception(sql::SQLException& e) noexcept
{
    const int code = e.getErrorCode();
    const std::string state(static_cast<const char*>(e.getSQLState()));
    return
        code == 2006 || code == 2013 || code == 2003 ||
        code == 2055 || code == 2026 ||
        (state.size() >= 2 && state.substr(0, 2) == "08");
}

runtime::DynamicValue read_typed_value(
    sql::ResultSet* rs,
    const std::string& field_name,
    sea::domain::FieldType field_type, bool unsigned_value = false)
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
    case FieldType::Decimal:
    case FieldType::File:
        return std::string(rs->getString(field_name));
    case FieldType::SmallInt:
        if (unsigned_value)
            return static_cast<std::uint16_t>(rs->getUInt(field_name));
        else
            return static_cast<std::int16_t>(rs->getInt(field_name));

    case FieldType::Int:
        if (unsigned_value)
            return static_cast<std::uint32_t>(rs->getUInt(field_name));
        else
            return static_cast<std::int32_t>(rs->getInt(field_name));

    case FieldType::BigInt:
        if (unsigned_value)
            return static_cast<std::uint64_t>(rs->getUInt64(field_name));
        else
            return static_cast<std::int64_t>(rs->getInt64(field_name));
    case FieldType::Float:
        return static_cast<double>(rs->getDouble(field_name));
    case FieldType::Bool:
        return rs->getBoolean(field_name);
    case FieldType::Json:
        return std::string(rs->getString(field_name));
    case FieldType::Binary:
    {
        std::istream* stream = rs->getBlob(field_name);

        std::vector<std::uint8_t> data(
            (std::istreambuf_iterator<char>(*stream)),
            std::istreambuf_iterator<char>()
            );

        return data;
    }
    case FieldType::Native:
    {
        auto* meta = rs->getMetaData();
        int column_index = rs->findColumn(field_name);
        return sea::infrastructure::runtime::NativeValue{
            .dialect = "mysql",
            .sql_type = std::string(static_cast<const char*>(meta->getColumnTypeName(column_index))),
            .value = std::string(rs->getString(field_name))
        };
    }
    }

    return std::monostate{};
}

runtime::DynamicRecord resultset_to_record(
    sql::ResultSet* rs,
    const sea::domain::Entity& entity)
{
    runtime::DynamicRecord record;
    for (const auto& field : entity.fields) {
        if(field.unsigned_value)
        {
            record[field.name] = read_typed_value(rs, field.name, field.type, field.unsigned_value);
        }
        else  record[field.name] = read_typed_value(rs, field.name, field.type);
    }
    return record;
}

void bindValue(
    sql::PreparedStatement* stmt,
    int index,
    const runtime::DynamicValue& value)
{
    try{
        if (std::holds_alternative<std::monostate>(value)) {
            stmt->setNull(index, 0);
        }
        else if (std::holds_alternative<std::string>(value)) {
            stmt->setString(index, std::get<std::string>(value));
        }
        else if (std::holds_alternative<std::int16_t>(value)) {
            stmt->setInt(index, std::get<std::int16_t>(value));
        }
        else if (std::holds_alternative<std::uint16_t>(value)) {
            stmt->setUInt(index, std::get<std::uint16_t>(value));
        }
        else if (std::holds_alternative<std::uint32_t>(value)) {
            stmt->setUInt(index, std::get<std::uint32_t>(value));
        }
        else if (std::holds_alternative<std::int32_t>(value)) {
            stmt->setInt(index, std::get<std::int32_t>(value));
        }
        else if (std::holds_alternative<std::uint64_t>(value)) {
            stmt->setUInt64(index, std::get<std::uint64_t>(value));
        }
        else if (std::holds_alternative<std::int64_t>(value)) {
            stmt->setInt64(index, std::get<std::int64_t>(value));
        }
        else if (std::holds_alternative<double>(value)) {
            stmt->setDouble(index, std::get<double>(value));
        }
        else if (std::holds_alternative<bool>(value)) {
            stmt->setBoolean(index, std::get<bool>(value));
        }
        else if (std::holds_alternative<nlohmann::json>(value)) {
            stmt->setString(index, std::get<nlohmann::json>(value).dump());
        }
        else if (std::holds_alternative<std::vector<std::uint8_t>>(value)) {
            const auto& bytes = std::get<std::vector<std::uint8_t>>(value);

            std::string binaryData(
                reinterpret_cast<const char*>(bytes.data()),
                bytes.size()
                );

            stmt->setString(index, binaryData);
        }
        else if (std::holds_alternative<runtime::NativeValue>(value)) {
            const auto& native = std::get<runtime::NativeValue>(value);

            if (native.value.is_string()) {
                stmt->setString(index, native.value.get<std::string>());
            } else {
                stmt->setString(index, native.value.dump());
            }
        }
        else {
            throw sea::sea_errors_handling::PersistenceException("[MYSQL] Unable to bind this value to a simple SQL column");
        }
    } catch(const sea::sea_errors_handling::PersistenceException& e){

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
    } catch (sql::SQLException& e) {
        if (is_connection_dead_exception(e)) {
            throw;
        }

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
        if (sea::domain::mysql_uses_binary_storage(field.type)) {
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
 * MODIFIE pour Module Transactions :
 * - Si une transaction est active (active_conn != nullptr), utilise cette connexion
 * - Sinon, acquire/release classique depuis le pool
 *
 * Flow :
 * 1. Soit récupère la connexion de transaction, soit acquire depuis le pool
 * 2. Exécuter la requête dans le thread pool (bloquant OK)
 * 3. Retourner le résultat dans le reactor (via future)
 * 4. Release la connexion SAUF si transaction active
 */
template <typename Result, typename Func>
seastar::future<Result> run_blocking_mysql(
    MysqlConnexionPool& pool,
    IBlockingExecutor& executor,
    sql::Connection* active_txn_conn,
    Func&& func)
{
    sql::Connection* conn = nullptr;
    const bool acquired_from_pool = (active_txn_conn == nullptr);

    if (acquired_from_pool) {
        conn = co_await pool.acquire();
    } else {
        conn = active_txn_conn;
    }

    // On capture l'etat dans des flags simples sans co_await dans le catch.
    // C++ coroutines interdit co_await directement dans un handler de catch.
    bool succeeded = false;
    bool connection_dead = false;
    std::exception_ptr pending_exception;

    try {
        auto result = co_await executor.submit(
            [conn, fn = std::forward<Func>(func)]() mutable -> Result {
                return fn(conn);
            }
            );

        // Succes : verifie l'etat de la connexion avant de la
        // remettre dans le pool.
        //
        // Important : meme en cas de "succes" de la lambda (return
        // sans exception qui s'echappe), la connexion peut etre dans
        // un etat invalide. C'est typiquement le cas quand un catch
        // INTERNE a la lambda a swallow une sql::SQLException (Lost
        // connection, server gone away) et retourne nullopt sans
        // re-throw. Sans ce check, on rebalancerait une connexion
        // morte dans le pool, ce qui crashe libssl au prochain
        // usage.
        if (acquired_from_pool) {
            bool closed = false;
            try {
                closed = conn->isClosed();
            } catch (...) {
                closed = true;
            }
            if (closed) {
                spdlog::get("sea.persistence")->warn(
                    "Connection isClosed after operation, discarding"
                    );
                co_await pool.discard_and_replace(conn);
            } else {
                pool.release(conn);
            }
        }
        co_return result;

    } catch (sql::SQLException& e) {
        // Detection des erreurs ou la connexion est devenue
        // definitivement inutilisable :
        //   2006 = MySQL server has gone away
        //   2013 = Lost connection to MySQL server during query
        //   2003 = Can't connect to MySQL server
        //   2055 = Lost connection during query (system error)
        //   2026 = SSL connection error
        // SQL state 08xxx = Connection exception (ISO/IEC 9075).
        const int code = e.getErrorCode();
        const std::string state(static_cast<const char*>(e.getSQLState()));
        connection_dead =
            code == 2006 || code == 2013 || code == 2003 ||
            code == 2055 || code == 2026 ||
            (state.size() >= 2 && state.substr(0, 2) == "08");

        if (connection_dead) {
            spdlog::get("sea.persistence")->warn(
                "Connection dead (code={}, state={}, msg={}), discarding",
                code, state, e.what()
                );
        }

        pending_exception = std::current_exception();

    } catch (...) {
        // Exception non-SQL : la connexion est presumee OK.
        pending_exception = std::current_exception();
    }

    // Hors du bloc catch : on peut faire co_await.
    if (acquired_from_pool) {
        if (connection_dead) {
            co_await pool.discard_and_replace(conn);
        } else {
            pool.release(conn);
        }
    }
    // Si on est dans une transaction (active_txn_conn != nullptr),
    // on ne touche pas a la connexion : in_transaction se chargera
    // du cleanup.

    std::rethrow_exception(pending_exception);
}

} // namespace anonyme

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
        _active_txn_connection,
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
                        sea::domain::mysql_uses_binary_storage(field->type)) {
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
            } catch (sql::SQLException& e) {
                if (is_connection_dead_exception(e)) {
                    spdlog::get("sea.persistence")->error("CREATE EXCEPTION (connection dead, will discard): {}",e.what());
                    throw;
                }
                spdlog::get("sea.persistence")->error("CREATE EXCEPTION: {}", e.what());

                return std::nullopt;
            }
        }
        );
}

/**
 * Récupère tous les enregistrements d'une entité
 */
seastar::future<std::vector<runtime::DynamicRecord>>
MySQLGenericRepository::find_all(const std::string& entity_name)
{
    auto& pool = _pool.local();
    co_return co_await run_blocking_mysql<std::vector<runtime::DynamicRecord>>(
        pool,
        *_executor,
        _active_txn_connection,
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
            } catch (sql::SQLException& e) {
                if (is_connection_dead_exception(e)) {
                    spdlog::get("sea.persistence")->error(
                        "find_all connection dead: {} (code={})",
                        e.what(), e.getErrorCode()
                        );
                    throw;
               }
                spdlog::get("sea.persistence")->error(
                    "find_all SQL error: {} (code={})",
                    e.what(), e.getErrorCode()
                    );
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
        _active_txn_connection,
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
            } catch (sql::SQLException& e) {
                if (is_connection_dead_exception(e)) {
                    spdlog::get("sea.persistence")->error(
                        "find_by_id connection dead: {} (code={})",
                        e.what(), e.getErrorCode()
                        );
                    throw;
                }
                spdlog::get("sea.persistence")->error(
                    "find_by_id SQL error: {} (code={})",
                    e.what(), e.getErrorCode()
                    );
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
        _active_txn_connection,
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
                if (sea::domain::mysql_uses_binary_storage(field->type)) {
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
            } catch (sql::SQLException& e) {
                if (is_connection_dead_exception(e)) {
                    spdlog::get("sea.persistence")->error(
                        "find_one_by_field connection dead: {} (code={})",
                        e.what(), e.getErrorCode()
                        );
                    throw;
                }
                spdlog::get("sea.persistence")->error(
                    "find_one_by_field SQL error: {} (code={})",
                    e.what(), e.getErrorCode()
                    );
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
        _active_txn_connection,
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
            } catch (sql::SQLException& e) {
                if (is_connection_dead_exception(e)) {
                    spdlog::get("sea.persistence")->error(
                        "remove connection dead: {} (code={})",
                        e.what(), e.getErrorCode()
                        );
                    throw;
                }
                spdlog::get("sea.persistence")->error(
                    "remove SQL error: {} (code={})",
                    e.what(), e.getErrorCode()
                    );
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
        _active_txn_connection,
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
                        sea::domain::mysql_uses_binary_storage(field->type)) {
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
            } catch (sql::SQLException& e) {
                if (is_connection_dead_exception(e)) {
                    spdlog::get("sea.persistence")->error(
                        "updae connection dead: {} (code={})",
                        e.what(), e.getErrorCode()
                        );
                    throw;
                }
                spdlog::get("sea.persistence")->error(
                    "update SQL error: {} (code={})",
                    e.what(), e.getErrorCode()
                    );
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
        throw std::runtime_error("insert_pivot: no value provided");
    }

    ValidationResult validation_result;
    if (!validate_sql_identifier(pivot_table, validation_result)) {
        throw std::runtime_error("insert_pivot: invalid pivot table name");
    }

    auto& pool = _pool.local();
    co_return co_await run_blocking_mysql<bool>(
        pool,
        *_executor,
        _active_txn_connection,
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

                // Helper pour detecter si une string est un UUID
                // Format : "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" (36 chars avec 4 tirets)
                auto is_uuid_string = [](const runtime::DynamicValue& v) -> bool {
                    if (!std::holds_alternative<std::string>(v)) return false;
                    const auto& s = std::get<std::string>(v);
                    if (s.size() != 36) return false;
                    return s[8] == '-' && s[13] == '-' && s[18] == '-' && s[23] == '-';
                };

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
                    //  Si c'est un UUID, wrap avec UUID_TO_BIN(?, 1)
                    if (is_uuid_string(bind_values[i])) {
                        query << "UUID_TO_BIN(?, 1)";
                    } else {
                        query << "?";
                    }
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

            } catch (sql::SQLException& e) {
                if (is_connection_dead_exception(e)) {
                    spdlog::get("sea.persistence")->error(
                        "insert_pivot connection dead: {} (code={})",
                        e.what(), e.getErrorCode()
                        );
                    throw;
                }

                // l'erreur pour faciliter le debug
                spdlog::get("sea.persistence")->error(
                    "insert_pivot SQL error: {} (code={})",
                    e.what(), e.getErrorCode()
                    );

                return false;
            } catch (const std::exception& e) {
                spdlog::get("sea.persistence")->error(
                    "insert_pivot error: {}", e.what()
                    );

                return false;
            }
        }
        );
}

seastar::future<bool>
MySQLGenericRepository::delete_pivot(
    const std::string& pivot_table,
    runtime::DynamicRecord values)
{
    using namespace sea::infrastructure::persistence::utilities;

    if (values.empty()) {
        throw std::runtime_error("delete_pivot: no value provided");
    }

    ValidationResult validation_result;
    if (!validate_sql_identifier(pivot_table, validation_result)) {
        throw std::runtime_error("delete_pivot: invalid pivot table name");
    }

    auto& pool = _pool.local();
    co_return co_await run_blocking_mysql<bool>(
        pool,
        *_executor,
        _active_txn_connection,
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

                // Meme heuristique UUID que dans insert_pivot
                auto is_uuid_string = [](const runtime::DynamicValue& v) -> bool {
                    if (!std::holds_alternative<std::string>(v)) return false;
                    const auto& s = std::get<std::string>(v);
                    if (s.size() != 36) return false;
                    return s[8] == '-' && s[13] == '-' && s[18] == '-' && s[23] == '-';
                };

                std::ostringstream query;
                query << "DELETE FROM `" << pivot_table << "` WHERE ";
                for (std::size_t i = 0; i < columns.size(); ++i) {
                    if (i > 0) {
                        query << " AND ";
                    }
                    query << "`" << columns[i] << "` = ";
                    if (is_uuid_string(bind_values[i])) {
                        query << "UUID_TO_BIN(?, 1)";
                    } else {
                        query << "?";
                    }
                }

                auto stmt = std::unique_ptr<sql::PreparedStatement>(
                    conn->prepareStatement(query.str())
                    );

                for (std::size_t i = 0; i < bind_values.size(); ++i) {
                    bindValue(stmt.get(), static_cast<int>(i + 1), bind_values[i]);
                }

                // executeUpdate retourne le nombre de lignes affectees.
                // > 0 -> au moins une association supprimee
                // = 0 -> aucune association ne correspondait
                const int affected = stmt->executeUpdate();
                return affected > 0;

            } catch (sql::SQLException& e) {
                if (is_connection_dead_exception(e)) {
                    spdlog::get("sea.persistence")->error(
                        "delete_pivot connection dead: {} (code={})",
                        e.what(), e.getErrorCode()
                        );
                    throw;
                }

                spdlog::get("sea.persistence")->error(
                    "delete_pivot SQL error: {} (code={})",
                    e.what(), e.getErrorCode()
                    );
                return false;
            } catch (const std::exception& e) {
                spdlog::get("sea.persistence")->error(
                    "delete_pivot error: {}", e.what()
                    );
                return false;
            }
        }
        );
}


seastar::future<bool>
MySQLGenericRepository::pivot_exists(
    const std::string& pivot_table,
    runtime::DynamicRecord values)
{
    using namespace sea::infrastructure::persistence::utilities;

    if (values.empty()) {
        throw std::runtime_error("pivot_exists: no value provided");
    }

    ValidationResult validation_result;
    if (!validate_sql_identifier(pivot_table, validation_result)) {
        throw std::runtime_error("pivot_exists: invalid pivot table name");
    }

    auto& pool = _pool.local();
    co_return co_await run_blocking_mysql<bool>(
        pool,
        *_executor,
        _active_txn_connection,
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

                auto is_uuid_string = [](const runtime::DynamicValue& v) -> bool {
                    if (!std::holds_alternative<std::string>(v)) return false;
                    const auto& s = std::get<std::string>(v);
                    if (s.size() != 36) return false;
                    return s[8] == '-' && s[13] == '-' && s[18] == '-' && s[23] == '-';
                };

                std::ostringstream query;
                query << "SELECT 1 FROM `" << pivot_table << "` WHERE ";
                for (std::size_t i = 0; i < columns.size(); ++i) {
                    if (i > 0) {
                        query << " AND ";
                    }
                    query << "`" << columns[i] << "` = ";
                    if (is_uuid_string(bind_values[i])) {
                        query << "UUID_TO_BIN(?, 1)";
                    } else {
                        query << "?";
                    }
                }
                query << " LIMIT 1";

                auto stmt = std::unique_ptr<sql::PreparedStatement>(
                    conn->prepareStatement(query.str())
                    );

                for (std::size_t i = 0; i < bind_values.size(); ++i) {
                    bindValue(stmt.get(), static_cast<int>(i + 1), bind_values[i]);
                }

                auto rs = std::unique_ptr<sql::ResultSet>(stmt->executeQuery());
                return rs->next();

            } catch (sql::SQLException& e) {
                if (is_connection_dead_exception(e)) {
                    spdlog::get("sea.persistence")->error(
                        "pivot_exist connection dead: {} (code={})",
                        e.what(), e.getErrorCode()
                        );
                    throw;
                }
                spdlog::get("sea.persistence")->error(
                    "pivot_exists SQL error: {} (code={})",
                    e.what(), e.getErrorCode()
                    );
                return false;
            } catch (const std::exception& e) {
                spdlog::get("sea.persistence")->error(
                    "pivot_exists error: {}", e.what()
                    );
                return false;
            }
        }
        );
}


// ────────────────────────────────────────────────────────────────────────
// in_transaction
//
// Pipeline :
// 1. Si deja dans une transaction : execute la lambda directement (no-op).
// 2. Acquire une connexion du pool (Seastar non-blocking).
// 3. setAutoCommit(false) sur cette connexion (operation bloquante → executor).
// 4. Stocke la connexion dans _active_txn_connection (les CRUD l'utiliseront).
// 5. Execute la lambda (peut faire plusieurs co_await create/update/etc).
// 6. Si lambda → true : COMMIT.
//    Si lambda → false : ROLLBACK.
//    Si exception : ROLLBACK + rethrow.
// 7. setAutoCommit(true) pour rendre la connexion "propre" au pool.
// 8. Release la connexion vers le pool.
// ────────────────────────────────────────────────────────────────────────
seastar::future<TransactionResult>
MySQLGenericRepository::in_transaction(
    std::function<seastar::future<bool>()> work)
{
    // Cas degenere : transaction imbriquee. On execute juste la lambda dans
    // la transaction parent (pas de begin/commit interne).
    if (is_in_transaction()) {
        spdlog::get("sea.persistence")->debug(
            "TXN Nested transaction detected, executing inline"
            );
        const bool ok = co_await work();
        co_return TransactionResult{
            .committed = ok,
            .error_message = ok ? "" : "Nested transaction returned false"
        };
    }

    auto& pool = _pool.local();

    // 1. Acquire une connexion (non bloquant Seastar)
    sql::Connection* conn = co_await pool.acquire();

    // 2. setAutoCommit(false) (operation bloquante → executor)
    try {
        co_await _executor->submit([conn]() {
            conn->setAutoCommit(false);
        });
    } catch (const std::exception& e) {
        // Si on n'arrive meme pas a desactiver l'autocommit, on release et abandonne.
        pool.release(conn);
        spdlog::get("sea.persistence")->error(
            "TXN Failed to disable autocommit: {}", e.what()
            );
        co_return TransactionResult{
            .committed = false,
            .error_message = std::string("Failed to begin transaction: ") + e.what()
        };
    }

    // 3. Stocke la connexion active (les CRUD vont l'utiliser via run_blocking_mysql)
    _active_txn_connection = conn;

    // 4. Execute la lambda
    bool should_commit = false;
    std::string error_message;

    try {
        should_commit = co_await work();
    } catch (const std::exception& e) {
        spdlog::get("sea.persistence")->error(
            "TXN Exception in transaction: {}", e.what()
            );
        error_message = std::string("Exception: ") + e.what();
        should_commit = false;
    } catch (...) {
        spdlog::get("sea.persistence")->error(
            "TXN Unknown exception in transaction"
            );
        error_message = "Unknown exception";
        should_commit = false;
    }

    // 5. COMMIT ou ROLLBACK selon le resultat de la lambda
    bool committed = false;
    try {
        if (should_commit) {
            co_await _executor->submit([conn]() {
                conn->commit();
            });
            committed = true;
            spdlog::get("sea.persistence")->debug("TXN COMMIT");
        } else {
            co_await _executor->submit([conn]() {
                conn->rollback();
            });
            committed = false;
            spdlog::get("sea.persistence")->debug("TXN ROLLBACK");
        }
    } catch (const std::exception& e) {
        spdlog::get("sea.persistence")->error(
            "TXN Failed to commit/rollback: {}", e.what()
            );
        error_message = std::string("Failed to finalize transaction: ") + e.what();
        committed = false;
    }

    // 6. Restore l'autocommit (la connexion retourne au pool en etat propre)
    try {
        co_await _executor->submit([conn]() {
            conn->setAutoCommit(true);
        });
    } catch (const std::exception& e) {
        spdlog::get("sea.persistence")->warn(
            "TXN Failed to restore autocommit: {} (continuing)", e.what()
            );
        // On continue quand meme : on doit liberer la connexion.
    }

    // 7. Reset le tracking + release
    _active_txn_connection = nullptr;
    pool.release(conn);

    co_return TransactionResult{
        .committed = committed,
        .error_message = std::move(error_message)
    };
}
// ═══════════════════════════════════════════════════════════════════
// PAGINATION
//
// A coller dans mysql_generic_repository.cpp JUSTE AVANT la ligne
// terminant le namespace :  } // namespace sea::infrastructure::persistence::mysql
//
// Strategie :
// - count          : SELECT COUNT(*) FROM table
// - list_page      : SELECT ... FROM table [ORDER BY field ASC/DESC] LIMIT ? OFFSET ?
//                    avec offset = (page - 1) * page_size
//                    + COUNT(*) pour calculer 'total'
// - list_offset    : idem que list_page mais sans calcul de page
// - list_cursor    : SELECT ... FROM table WHERE cursor_field > ? ORDER BY cursor_field ASC LIMIT ?
//                    (ou < ? + DESC selon sort_desc)
//
// Toutes les requetes utilisent les helpers existants :
// - get_required_entity, resolve_table_name, validate_sql_identifier
// - build_select_columns, resultset_to_record
//
// Securite SQL :
// - LIMIT / OFFSET / COUNT  : valeurs scalaires non-injectables (size_t)
// - ORDER BY field          : on valide field via validate_sql_identifier
// - cursor value            : bound via setString (prepared statement)
// ═══════════════════════════════════════════════════════════════════

seastar::future<std::size_t>
MySQLGenericRepository::count(const std::string& entity_name)
{
    auto& pool = _pool.local();
    co_return co_await run_blocking_mysql<std::size_t>(
        pool,
        *_executor,
        _active_txn_connection,
        [this, entity_name](sql::Connection* conn) -> std::size_t {
            using namespace sea::infrastructure::persistence::utilities;
            ValidationResult validation_result;
            const auto* entity =
                get_required_entity(*_schema_registry, entity_name, validation_result);
            if (entity == nullptr) {
                return 0;
            }
            const std::string table_name = resolve_table_name(*entity);
            if (!validate_sql_identifier(table_name, validation_result)) {
                return 0;
            }
            try {
                std::ostringstream sql;
                sql << "SELECT COUNT(*) FROM `" << table_name << "`";
                auto stmt =
                    std::unique_ptr<sql::Statement>(conn->createStatement());
                auto rs =
                    std::unique_ptr<sql::ResultSet>(stmt->executeQuery(sql.str()));
                if (!rs->next()) {
                    return 0;
                }
                return static_cast<std::size_t>(rs->getInt64(1));
            } catch (sql::SQLException& e) {
                if (is_connection_dead_exception(e)) {
                    spdlog::get("sea.persistence")->error(
                        "count connexion dead (code={}, state={}): {}",
                        e.getErrorCode(), e.getSQLState(), e.what()
                        );
                    throw;
                }
                spdlog::get("sea.persistence")->error(
                    "count SQL error (code={}, state={}): {}",
                    e.getErrorCode(), e.getSQLState(), e.what()
                    );
                return 0;
            }
        }
        );
}

seastar::future<sea::infrastructure::persistence::PageResult>
MySQLGenericRepository::list_page(
    const std::string& entity_name,
    const PageRequest& request)
{
    auto& pool = _pool.local();
    co_return co_await run_blocking_mysql<PageResult>(
        pool,
        *_executor,
        _active_txn_connection,
        [this, entity_name, request](sql::Connection* conn) -> PageResult {
            using namespace sea::infrastructure::persistence::utilities;
            PageResult result;

            ValidationResult validation_result;
            const auto* entity =
                get_required_entity(*_schema_registry, entity_name, validation_result);
            if (entity == nullptr) {
                return result;
            }
            const std::string table_name = resolve_table_name(*entity);
            if (!validate_sql_identifier(table_name, validation_result)) {
                return result;
            }

            // Sort field doit etre un identifiant SQL valide si fourni
            std::string order_clause;
            if (request.sort_field.has_value()) {
                const std::string& sf = *request.sort_field;
                if (!validate_sql_identifier(sf, validation_result)) {
                    return result;
                }
                order_clause = " ORDER BY `" + sf + "` " +
                               (request.sort_desc ? "DESC" : "ASC");
            }

            try {
                // 1) Compter le total
                {
                    std::ostringstream csql;
                    csql << "SELECT COUNT(*) FROM `" << table_name << "`";
                    auto cstmt = std::unique_ptr<sql::Statement>(conn->createStatement());
                    auto crs = std::unique_ptr<sql::ResultSet>(cstmt->executeQuery(csql.str()));
                    if (crs->next()) {
                        result.total = static_cast<std::size_t>(crs->getInt64(1));
                    }
                }

                // 2) Fetch la page
                const std::size_t page = request.page > 0 ? request.page : 1;
                const std::size_t offset = (page - 1) * request.page_size;

                std::ostringstream sql;
                sql << "SELECT " << build_select_columns(*entity)
                    << " FROM `" << table_name << "`"
                    << order_clause
                    << " LIMIT " << request.page_size
                    << " OFFSET " << offset;

                auto stmt = std::unique_ptr<sql::Statement>(conn->createStatement());
                auto rs = std::unique_ptr<sql::ResultSet>(stmt->executeQuery(sql.str()));
                while (rs->next()) {
                    result.items.push_back(resultset_to_record(rs.get(), *entity));
                }
            } catch (sql::SQLException& e) {
                if (is_connection_dead_exception(e)) {
                    spdlog::get("sea.persistence")->error(
                        "list_page connexion dead (code={}, state={}): {}",
                        e.getErrorCode(), e.getSQLState(), e.what()
                        );
                    throw;
                }
                spdlog::get("sea.persistence")->error(
                    "list_page SQL error (code={}, state={}): {}",
                    e.getErrorCode(), e.getSQLState(), e.what()
                    );
                // Sur erreur SQL, on retourne ce qu'on a (probablement vide)
                return result;
            }

            return result;
        }
        );
}

seastar::future<sea::infrastructure::persistence::OffsetResult>
MySQLGenericRepository::list_offset(
    const std::string& entity_name,
    const OffsetRequest& request)
{
    auto& pool = _pool.local();
    co_return co_await run_blocking_mysql<OffsetResult>(
        pool,
        *_executor,
        _active_txn_connection,
        [this, entity_name, request](sql::Connection* conn) -> OffsetResult {
            using namespace sea::infrastructure::persistence::utilities;
            OffsetResult result;

            ValidationResult validation_result;
            const auto* entity =
                get_required_entity(*_schema_registry, entity_name, validation_result);
            if (entity == nullptr) {
                return result;
            }
            const std::string table_name = resolve_table_name(*entity);
            if (!validate_sql_identifier(table_name, validation_result)) {
                return result;
            }

            std::string order_clause;
            if (request.sort_field.has_value()) {
                const std::string& sf = *request.sort_field;
                if (!validate_sql_identifier(sf, validation_result)) {
                    return result;
                }
                order_clause = " ORDER BY `" + sf + "` " +
                               (request.sort_desc ? "DESC" : "ASC");
            }

            try {
                // 1) Compter le total
                {
                    std::ostringstream csql;
                    csql << "SELECT COUNT(*) FROM `" << table_name << "`";
                    auto cstmt = std::unique_ptr<sql::Statement>(conn->createStatement());
                    auto crs = std::unique_ptr<sql::ResultSet>(cstmt->executeQuery(csql.str()));
                    if (crs->next()) {
                        result.total = static_cast<std::size_t>(crs->getInt64(1));
                    }
                }

                // 2) Fetch
                std::ostringstream sql;
                sql << "SELECT " << build_select_columns(*entity)
                    << " FROM `" << table_name << "`"
                    << order_clause
                    << " LIMIT " << request.limit
                    << " OFFSET " << request.offset;

                auto stmt = std::unique_ptr<sql::Statement>(conn->createStatement());
                auto rs = std::unique_ptr<sql::ResultSet>(stmt->executeQuery(sql.str()));
                while (rs->next()) {
                    result.items.push_back(resultset_to_record(rs.get(), *entity));
                }
            } catch (sql::SQLException& e) {
                if (is_connection_dead_exception(e)) {
                    spdlog::get("sea.persistence")->error(
                        "list_offset connection dead (code={}, state={}): {}",
                        e.getErrorCode(), e.getSQLState(), e.what()
                        );
                    throw;
                }
                spdlog::get("sea.persistence")->error(
                    "list_offset SQL error (code={}, state={}): {}",
                    e.getErrorCode(), e.getSQLState(), e.what()
                    );
                return result;
            }

            return result;
        }
        );
}

seastar::future<sea::infrastructure::persistence::CursorResult>
MySQLGenericRepository::list_cursor(
    const std::string& entity_name,
    const CursorRequest& request)
{
    auto& pool = _pool.local();
    co_return co_await run_blocking_mysql<CursorResult>(
        pool,
        *_executor,
        _active_txn_connection,
        [this, entity_name, request](sql::Connection* conn) -> CursorResult {
            using namespace sea::infrastructure::persistence::utilities;
            CursorResult result;

            ValidationResult validation_result;
            const auto* entity =
                get_required_entity(*_schema_registry, entity_name, validation_result);
            if (entity == nullptr) {
                return result;
            }
            const std::string table_name = resolve_table_name(*entity);
            if (!validate_sql_identifier(table_name, validation_result) ||
                !validate_sql_identifier(request.cursor_field, validation_result)) {
                return result;
            }

            // Fetch limit + 1 pour determiner s'il y a une page suivante
            // (technique standard : on lit 1 ligne de plus, si elle existe
            // c'est qu'il y a un next_cursor)
            const std::size_t fetch_limit = request.limit + 1;

            try {
                std::ostringstream sql;
                sql << "SELECT " << build_select_columns(*entity)
                    << " FROM `" << table_name << "`";

                const bool has_after = request.after.has_value();
                if (has_after) {
                    // ASC : WHERE cursor_field > ?
                    // DESC: WHERE cursor_field < ?
                    sql << " WHERE `" << request.cursor_field << "` "
                        << (request.sort_desc ? "<" : ">") << " ?";
                }

                sql << " ORDER BY `" << request.cursor_field << "` "
                    << (request.sort_desc ? "DESC" : "ASC")
                    << " LIMIT " << fetch_limit;

                auto stmt = std::unique_ptr<sql::PreparedStatement>(
                    conn->prepareStatement(sql.str())
                    );
                if (has_after) {
                    stmt->setString(1, *request.after);
                }
                auto rs = std::unique_ptr<sql::ResultSet>(stmt->executeQuery());

                std::vector<runtime::DynamicRecord> fetched;
                while (rs->next()) {
                    fetched.push_back(resultset_to_record(rs.get(), *entity));
                }

                // Si on a fetch limit+1 lignes, il y a une page suivante.
                // On retire la derniere et on construit next_cursor a partir
                // de la valeur cursor_field du dernier element conserve.
                if (fetched.size() > request.limit) {
                    fetched.pop_back();
                    if (!fetched.empty()) {
                        const auto& last = fetched.back();
                        const auto it = last.find(request.cursor_field);
                        if (it != last.end()) {
                            // Le cursor_field est typiquement un id/uuid → string.
                            // On tente plusieurs types.
                            if (std::holds_alternative<std::string>(it->second)) {
                                result.next_cursor = std::get<std::string>(it->second);
                            } else if (std::holds_alternative<std::int64_t>(it->second)) {
                                result.next_cursor =
                                    std::to_string(std::get<std::int64_t>(it->second));
                            }
                        }
                    }
                }

                result.items = std::move(fetched);
            } catch (sql::SQLException& e) {
                if (is_connection_dead_exception(e)) {
                    spdlog::get("sea.persistence")->error(
                        "list_cursor connection dead (code={}, state={}): {}",
                        e.getErrorCode(), e.getSQLState(), e.what()
                        );
                    throw;
                }
                spdlog::get("sea.persistence")->error(
                    "list_cursor SQL errior (code={}, state={}): {}",
                    e.getErrorCode(), e.getSQLState(), e.what()
                    );
                return result;
            }

            return result;
        }
        );
}
// ─────────────────────────────────────────────────────────────
// increment_field
//
// UPDATE `<table>` SET `<field>` = `<field>` + ? WHERE `id` = ?
//
// Particularités :
//   - `entity_name` est utilisé directement comme nom de table.
//     On ne passe PAS par le SchemaRuntimeRegistry car cette
//     méthode doit aussi servir pour les tables système
//     (sea_files notamment), absentes du registry.
//
//   - L'`id` est détecté heuristiquement comme UUID (36 chars
//     avec dashes aux bonnes positions). Si oui, on wrap avec
//     UUID_TO_BIN(?, 1). Si non, on passe l'`id` tel quel.
//     Cohérent avec le pattern d'insert_pivot.
//
//   - `field_name` et `entity_name` sont validés comme identifiants
//     SQL pour éviter toute injection (pas de backtick, pas de
//     espace, etc.).
// ─────────────────────────────────────────────────────────────
seastar::future<bool>
MySQLGenericRepository::increment_field(
    const std::string& entity_name,
    const std::string& id,
    const std::string& field_name,
    std::int64_t delta)
{
    auto& pool = _pool.local();
    co_return co_await run_blocking_mysql<bool>(
        pool,
        *_executor,
        _active_txn_connection,
        [entity_name, id, field_name, delta](sql::Connection* conn) -> bool {
            using namespace sea::infrastructure::persistence::utilities;
            ValidationResult validation_result;

            // Validation : entity_name et field_name doivent être
            // des identifiants SQL valides (anti-injection).
            if (!validate_sql_identifier(entity_name, validation_result)) {
                return false;
            }
            if (!validate_sql_identifier(field_name, validation_result)) {
                return false;
            }

            // Détection heuristique UUID : si l'`id` ressemble à un
            // UUID v4 (36 chars, dashes aux positions canoniques),
            // on wrap avec UUID_TO_BIN. Sinon on passe tel quel.
            // Cohérent avec insert_pivot dans ce même fichier.
            const bool id_is_uuid =
                id.size() == 36 &&
                id[8] == '-' && id[13] == '-' &&
                id[18] == '-' && id[23] == '-';

            try {
                std::ostringstream sql;
                sql << "UPDATE `" << entity_name << "` "
                    << "SET `" << field_name << "` = `" << field_name << "` + ? "
                    << "WHERE `id` = "
                    << (id_is_uuid ? "UUID_TO_BIN(?, 1)" : "?");

                auto stmt = std::unique_ptr<sql::PreparedStatement>(
                    conn->prepareStatement(sql.str())
                    );

                stmt->setInt64(1, delta);
                stmt->setString(2, id);

                const int affected_rows = stmt->executeUpdate();
                return affected_rows == 1;
            } catch (sql::SQLException& e) {
                if (is_connection_dead_exception(e)) {
                    spdlog::get("sea.persistence")->error(
                        "increment_field connection dead (code={}, state={}): {}",
                        e.getErrorCode(), e.getSQLState(), e.what()
                        );
                    throw;
                }
                spdlog::get("sea.persistence")->error(
                    "increment_field SQL error(code={}, state={}): {}",
                    e.getErrorCode(), e.getSQLState(), e.what()
                    );
                return false;
            }
        }
        );
}
seastar::future<bool>
MySQLGenericRepository::decrement_field_if_positive(
    const std::string& entity_name,
    const std::string& id,
    const std::string& field_name)
{
    auto& pool = _pool.local();
    co_return co_await run_blocking_mysql<bool>(
        pool,
        *_executor,
        _active_txn_connection,
        [entity_name, id, field_name](sql::Connection* conn) -> bool {
            using namespace sea::infrastructure::persistence::utilities;
            ValidationResult validation_result;

            // Validation anti-injection (identique à increment_field).
            if (!validate_sql_identifier(entity_name, validation_result)) {
                return false;
            }
            if (!validate_sql_identifier(field_name, validation_result)) {
                return false;
            }

            // Détection heuristique UUID (identique à increment_field).
            const bool id_is_uuid =
                id.size() == 36 &&
                id[8] == '-' && id[13] == '-' &&
                id[18] == '-' && id[23] == '-';

            try {
                // Décrément conditionnel : la clause `f > 0` est
                // évaluée par MySQL dans le MÊME verrou de ligne que
                // l'UPDATE. Deux décréments concurrents ne peuvent
                // donc pas faire passer le compteur sous zéro :
                // celui qui voit déjà 0 n'affecte aucune ligne.
                std::ostringstream sql;
                sql << "UPDATE `" << entity_name << "` "
                    << "SET `" << field_name << "` = `" << field_name << "` - 1 "
                    << "WHERE `id` = "
                    << (id_is_uuid ? "UUID_TO_BIN(?, 1)" : "?")
                    << " AND `" << field_name << "` > 0";

                auto stmt = std::unique_ptr<sql::PreparedStatement>(
                    conn->prepareStatement(sql.str())
                    );

                stmt->setString(1, id);

                const int affected_rows = stmt->executeUpdate();
                // 1 ligne affectée = décrément effectué.
                // 0 ligne = id inexistant OU champ déjà <= 0.
                return affected_rows == 1;
            } catch (sql::SQLException& e) {
                if (is_connection_dead_exception(e)) {
                    spdlog::get("sea.persistence")->error(
                        "decrement_field_if_positive connection dead (code={}, state={}): {}",
                        e.getErrorCode(), e.getSQLState(), e.what()
                        );
                    throw;
                }
                spdlog::get("sea.persistence")->error(
                    "decrement_field_if_positive SQL error (code={}, state={}): {}",
                    e.getErrorCode(), e.getSQLState(), e.what()
                    );
                return false;
            }
        }
        );
}
} // namespace sea::infrastructure::persistence::mysql