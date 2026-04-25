#pragma once

#include <persistence/i_generic_repository.h>
#include "runtime/schema_runtime_registry.h"
#include <memory>
#include <cppconn/prepared_statement.h>
#include <seastar/core/future.hh>
#include <seastar/core/semaphore.hh>
#include <seastar/core/sharded.hh>
#include <seastar/core/thread.hh>
#include "mysqlconnexionpool.h"

namespace sea::infrastructure::persistence::mysql {

// Placeholder futur pour l'implémentation MySQL
class MySQLGenericRepository final : public IGenericRepository
{

public:

    MySQLGenericRepository(seastar::sharded<MysqlConnexionPool>& pool, std::shared_ptr<runtime::SchemaRuntimeRegistry> schema_registry);
    // IGenericRepository interface
    seastar::future<std::optional<runtime::DynamicRecord>> create(const std::string &entity_name, runtime::DynamicRecord record);
    seastar::future<std::vector<runtime::DynamicRecord>> find_all(const std::string &entity_name);
    seastar::future<std::optional<runtime::DynamicRecord>> find_by_id(const std::string &entity_name, const std::string &id);
    seastar::future<bool> remove(const std::string &entity_name, const std::string &id);
    seastar::future<sea::infrastructure::persistence::UpdateResponse> update(const std::string &entity_name, const std::string &id, runtime::DynamicRecord record);
    seastar::future<bool> insert_pivot(const std::string& pivot_table,
                                       runtime::DynamicRecord values);
private:
    seastar::sharded<MysqlConnexionPool>& _pool;
    std::shared_ptr<runtime::SchemaRuntimeRegistry> _schema_registry;
};
} // namespace sea::infrastructure::persistence:mysql

