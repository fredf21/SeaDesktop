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
#include "thread_pool_execution/i_blocking_executor.h"
namespace sea::infrastructure::persistence::mysql {

// Placeholder futur pour l'implémentation MySQL
class MySQLGenericRepository final : public IGenericRepository
{

public:

    MySQLGenericRepository(seastar::sharded<MysqlConnexionPool>& pool, std::shared_ptr<runtime::SchemaRuntimeRegistry> schema_registry,std::shared_ptr<IBlockingExecutor> executor);
    // IGenericRepository interface
    seastar::future<std::optional<runtime::DynamicRecord>> create(const std::string &entity_name, runtime::DynamicRecord record) override;
    seastar::future<std::vector<runtime::DynamicRecord>> find_all(const std::string &entity_name) override;
    seastar::future<std::optional<runtime::DynamicRecord>> find_by_id(const std::string &entity_name, const std::string &id) override;
    seastar::future<bool> remove(const std::string &entity_name, const std::string &id) override;
    seastar::future<sea::infrastructure::persistence::UpdateResponse> update(const std::string &entity_name, const std::string &id, runtime::DynamicRecord record) override;
    seastar::future<bool> insert_pivot(const std::string& pivot_table,
                                       runtime::DynamicRecord values) override;
    seastar::future<std::optional<runtime::DynamicRecord>>
    find_one_by_field(const std::string& entity_name,
                      const std::string& field_name,
                      const std::string& value) override;
private:
    seastar::sharded<MysqlConnexionPool>& _pool;
    std::shared_ptr<runtime::SchemaRuntimeRegistry> _schema_registry;
    std::shared_ptr<IBlockingExecutor> _executor;
};
} // namespace sea::infrastructure::persistence:mysql

