#pragma once
#include <seastar/http/httpd.hh>
#include <memory>
#include <string>
#include "database_config.h"
#include "thread_pool_execution/i_blocking_executor.h"

namespace sea::application { class AuthService; }
namespace sea::infrastructure::runtime {
class GenericCrudEngine;
class SchemaRuntimeRegistry;
}

namespace sea::http::handlers::crud {

class CreateHandler final : public seastar::httpd::handler_base {
public:
    CreateHandler(std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
                  std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry> registry,
                  std::string entity_name,
                  std::shared_ptr<sea::application::AuthService> auth_service,
                  sea::domain::DatabaseType db_type,
                  std::shared_ptr<IBlockingExecutor> blocking_executor);

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override;

private:
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine_;
    std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry> registry_;
    std::string entity_name_;
    std::shared_ptr<sea::application::AuthService> auth_service_;
    sea::domain::DatabaseType db_type_;
    std::shared_ptr<IBlockingExecutor> blocking_executor_;
};

}
