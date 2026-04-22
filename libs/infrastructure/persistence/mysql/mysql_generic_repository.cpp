#include "mysql_generic_repository.h"

#include <cppconn/connection.h>
#include <cppconn/exception.h>
#include <cppconn/metadata.h>
#include <cppconn/resultset.h>
#include <cppconn/resultset_metadata.h>
#include "persistence/utilities.h"

namespace sea::infrastructure::persistence::mysql {
namespace {
/// \brief Lit une valeur SQL selon le type métier du champ.
///
/// Le typage suit ton FieldType métier.
///
/// \param rs ResultSet courant
/// \param field_name Nom du champ/colonne
/// \param field_type Type métier du champ
///
/// \return Valeur convertie en DynamicValue
runtime::DynamicValue read_typed_value(sql::ResultSet* rs,
                                       const std::string& field_name,
                                       sea::domain::FieldType field_type) {
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

/// \brief Convertit une ligne SQL en DynamicRecord.
///
/// La conversion suit l'ordre et les types décrits dans l'entité.
///
/// \param rs ResultSet positionné sur une ligne valide
/// \param entity Entité métier correspondante
///
/// \return Record reconstruit
runtime::DynamicRecord resultset_to_record(sql::ResultSet* rs,
                                           const sea::domain::Entity& entity) {
    runtime::DynamicRecord record;

    for (const auto& field : entity.fields) {
        record[field.name] = read_typed_value(rs, field.name, field.type);
    }

    return record;
}

/// \brief Lie une valeur dynamique à un paramètre SQL préparé.
///
/// Cette fonction convertit un DynamicValue vers l'API MySQL Connector/C++.
///
/// \param stmt Statement préparé
/// \param index Position du paramètre SQL (commence à 1)
/// \param value Valeur à lier
void bindValue(sql::PreparedStatement *stmt, int index, const runtime::DynamicValue &value)
{
    if (std::holds_alternative<std::monostate>(value)) {
        stmt->setNull(index, 0);
    }
    else if (std::holds_alternative<std::string>(value)) {
        stmt->setString(index, std::get<std::string>(value));
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
}
} // namespace

MySQLGenericRepository::MySQLGenericRepository(std::unique_ptr<mysql::MySQLConnector> mysqlconnector, const runtime::SchemaRuntimeRegistry& schema_registry) :
    _mysqlConnector(std::move(mysqlconnector))
    , _schema_registry(schema_registry)
{
    _mysqlConnection = _mysqlConnector->createConnection();
}

std::optional<sea::infrastructure::runtime::DynamicRecord> MySQLGenericRepository::create(const std::string &entity_name, runtime::DynamicRecord record)
{
    using namespace sea::infrastructure::persistence::utilities;
    ValidationResult validation_result;

    const auto* entity = get_required_entity(_schema_registry, entity_name, validation_result);
    if (entity == nullptr) {
        return std::nullopt;
    }

    const std::string table_name = resolve_table_name(*entity);

    if (!validate_sql_identifier(table_name, validation_result)) {
        return std::nullopt;
    }

    if (!validate_record_keys(_schema_registry, entity_name, record, validation_result)) {
        return std::nullopt;
    }

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
            sql << "?";
        }

        sql << ")";

        auto stmt = std::unique_ptr<sql::PreparedStatement>(
            _mysqlConnection->prepareStatement(sql.str())
            );

        for (std::size_t i = 0; i < columns.size(); ++i) {
            const auto* value = get_required_value(record, columns[i], validation_result);
            if (value == nullptr) {
                return std::nullopt;
            }

            bindValue(stmt.get(), static_cast<int>(i + 1), *value);
        }

        stmt->execute();
        return record;

    } catch (const sql::SQLException&) {
        return std::nullopt;
    }
}

std::vector<sea::infrastructure::runtime::DynamicRecord> MySQLGenericRepository::find_all(const std::string &entity_name) const
{
    using namespace sea::infrastructure::persistence::utilities;
    ValidationResult validation_result;
    const auto* entity = get_required_entity(_schema_registry, entity_name, validation_result);
    if (entity == nullptr) {
        return {};
    }

    const std::string table_name = resolve_table_name(*entity);

    if (!validate_sql_identifier(table_name, validation_result)) {
        return {};
    }

    try {

        std::ostringstream sql;
        sql << "SELECT * FROM `" << table_name << "`";

        auto stmt = std::unique_ptr<sql::Statement>(_mysqlConnection->createStatement());
        auto rs = std::unique_ptr<sql::ResultSet>(stmt->executeQuery(sql.str()));

        std::vector<runtime::DynamicRecord> results;

        while (rs->next()) {
            results.push_back(resultset_to_record(rs.get(), *entity));
        }

        return results;

    } catch (const sql::SQLException&) {
        return {};
    }
}

std::optional<sea::infrastructure::runtime::DynamicRecord> MySQLGenericRepository::find_by_id(const std::string &entity_name, const std::string &id) const
{
    using namespace sea::infrastructure::persistence::utilities;
    ValidationResult validation_result;
    const auto* entity = get_required_entity(_schema_registry, entity_name, validation_result);
    if (entity == nullptr) {
        return std::nullopt;
    }

    const std::string table_name = resolve_table_name(*entity);

    if (!validate_sql_identifier(table_name, validation_result)) {
        return std::nullopt;
    }

    try {

        std::ostringstream sql;
        sql << "SELECT * FROM `" << table_name << "` WHERE `id` = ? LIMIT 1";

        auto stmt = std::unique_ptr<sql::PreparedStatement>(
            _mysqlConnection->prepareStatement(sql.str())
            );

        stmt->setString(1, id);

        auto rs = std::unique_ptr<sql::ResultSet>(stmt->executeQuery());

        if (!rs->next()) {
            return std::nullopt;
        }

        return resultset_to_record(rs.get(), *entity);

    } catch (const sql::SQLException&) {
        return std::nullopt;
    }
}

bool MySQLGenericRepository::remove(const std::string &entity_name, const std::string &id)
{
    using namespace sea::infrastructure::persistence::utilities;
    ValidationResult validation_result;


    const auto* entity = get_required_entity(_schema_registry, entity_name, validation_result);
    if (entity == nullptr) {
        return false;
    }

    const std::string table_name = resolve_table_name(*entity);

    if (!validate_sql_identifier(table_name, validation_result)) {
        return false;
    }

    try {

        std::ostringstream sql;
        sql << "DELETE FROM `" << table_name << "` WHERE `id` = ?";

        auto stmt = std::unique_ptr<sql::PreparedStatement>(
            _mysqlConnection->prepareStatement(sql.str())
            );

        stmt->setString(1, id);

        const int affected_rows = stmt->executeUpdate();
        return affected_rows > 0;

    } catch (const sql::SQLException&) {
        return false;
    }
}

sea::infrastructure::persistence::UpdateResponse MySQLGenericRepository::update(const std::string &entity_name, const std::string &id, runtime::DynamicRecord record)
{
    using namespace sea::infrastructure::persistence::utilities;
    ValidationResult validation_result;
    const auto* entity = get_required_entity(_schema_registry, entity_name, validation_result);
    if (entity == nullptr) {
        return {.status = false, .record = {}};
    }

    const std::string table_name = resolve_table_name(*entity);

    if (!validate_sql_identifier(table_name, validation_result)) {
        return {.status = false, .record = {}};
    }

    if (!validate_record_keys(_schema_registry, entity_name, record, validation_result)) {
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
            sql << "`" << columns[i] << "` = ?";
        }

        sql << " WHERE `id` = ?";

        auto stmt = std::unique_ptr<sql::PreparedStatement>(
            _mysqlConnection->prepareStatement(sql.str())
            );

        int param_index = 1;

        for (const auto& column : columns) {
            const auto* value = get_required_value(record, column, validation_result);
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

        auto updated = find_by_id(entity_name, id);
        if (!updated.has_value()) {
            return {.status = false, .record = {}};
        }

        return {.status = true, .record = std::move(*updated)};

    } catch (const sql::SQLException&) {
        return {.status = false, .record = {}};
    }
}


}