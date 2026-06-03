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
    MySQLGenericRepository(seastar::sharded<MysqlConnexionPool>& pool,
                           std::shared_ptr<runtime::SchemaRuntimeRegistry> schema_registry,
                           std::shared_ptr<IBlockingExecutor> executor);

    // IGenericRepository interface
    seastar::future<std::optional<runtime::DynamicRecord>> create(const std::string &entity_name, runtime::DynamicRecord record) override;
    seastar::future<std::vector<runtime::DynamicRecord>> find_all(const std::string &entity_name) override;
    seastar::future<std::optional<runtime::DynamicRecord>> find_by_id(const std::string &entity_name, const std::string &id) override;
    seastar::future<bool> remove(const std::string &entity_name, const std::string &id) override;
    seastar::future<sea::infrastructure::persistence::UpdateResponse> update(const std::string &entity_name, const std::string &id, runtime::DynamicRecord record) override;
    seastar::future<bool> insert_pivot(const std::string& pivot_table,
                                       runtime::DynamicRecord values) override;
    seastar::future<bool> delete_pivot(const std::string &pivot_table, runtime::DynamicRecord values) override;
    seastar::future<bool> pivot_exists(const std::string &pivot_table, runtime::DynamicRecord values) override;
    seastar::future<std::optional<runtime::DynamicRecord>>
    find_one_by_field(const std::string& entity_name,
                      const std::string& field_name,
                      const std::string& value) override;

    // Pagination — 3 modes + count
    seastar::future<PageResult>
    list_page(const std::string& entity_name,
              const PageRequest& request) override;

    seastar::future<OffsetResult>
    list_offset(const std::string& entity_name,
                const OffsetRequest& request) override;

    seastar::future<CursorResult>
    list_cursor(const std::string& entity_name,
                const CursorRequest& request) override;

    seastar::future<std::size_t>
    count(const std::string& entity_name) override;

    // Transactions ACID
    seastar::future<TransactionResult> in_transaction(
        std::function<seastar::future<bool>()> work
        ) override;
    // increment_field : atomique en mode mono-shard (Seastar shared-nothing
    // garantit qu'un shard est mono-thread). Cf. IGenericRepository pour la
    // doc complète.
    seastar::future<bool>
    increment_field(const std::string& entity_name,
                    const std::string& id,
                    const std::string& field_name,
                    std::int64_t delta) override;

    seastar::future<bool> decrement_field_if_positive(const std::string &entity_name, const std::string &id, const std::string &field_name) override;

private:
    seastar::sharded<MysqlConnexionPool>& _pool;
    std::shared_ptr<runtime::SchemaRuntimeRegistry> _schema_registry;
    std::shared_ptr<IBlockingExecutor> _executor;

    // Connexion active si on est dans une transaction.
    // - nullptr : pas de transaction → run_blocking_mysql acquire/release du pool
    // - non-null : transaction active → run_blocking_mysql utilise CETTE connexion
    //
    // Avec Seastar shared-nothing, chaque shard a son propre repository,
    // donc cette variable n'a pas de probleme de concurrence (1 shard = 1 thread).
    sql::Connection* _active_txn_connection = nullptr;

    // Helpers internes
    bool is_in_transaction() const noexcept { return _active_txn_connection != nullptr; }


};

} // namespace sea::infrastructure::persistence:mysql