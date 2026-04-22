#pragma once

#include <persistence/i_generic_repository.h>
#include "mysql_connector.h"
#include "runtime/schema_runtime_registry.h"
#include <memory>
#include <cppconn/prepared_statement.h>
namespace sea::infrastructure::persistence::mysql {

// Placeholder futur pour l'implémentation MySQL
class MySQLGenericRepository final : public IGenericRepository
{

public:

    MySQLGenericRepository(std::unique_ptr<mysql::MySQLConnector> mysqlconnector, const runtime::SchemaRuntimeRegistry& schema_registry);
    // IGenericRepository interface
    std::optional<runtime::DynamicRecord> create(const std::string &entity_name, runtime::DynamicRecord record);
    std::vector<runtime::DynamicRecord> find_all(const std::string &entity_name) const;
    std::optional<runtime::DynamicRecord> find_by_id(const std::string &entity_name, const std::string &id) const;
    bool remove(const std::string &entity_name, const std::string &id);
    UpdateResponse update(const std::string &entity_name, const std::string &id, runtime::DynamicRecord record);
private:
    std::unique_ptr<mysql::MySQLConnector> _mysqlConnector;
    std::unique_ptr<sql::Connection> _mysqlConnection;
    const runtime::SchemaRuntimeRegistry& _schema_registry;
};

} // namespace sea::infrastructure::persistence:mysql

