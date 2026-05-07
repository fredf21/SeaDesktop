#pragma once
#include "thread_pool_execution/i_blocking_executor.h"
#include <seastar/http/httpd.hh>
#include <memory>
#include <string>

namespace sea::application { class AuthService; }
namespace sea::infrastructure::runtime {
class GenericCrudEngine;
class SchemaRuntimeRegistry;
}

// forward declaration
namespace sea::http::handlers::access_control {
class ResourceAuthorizationHelper;
}

namespace sea::http::handlers::crud {

class UpdateHandler final : public seastar::httpd::handler_base {
public:
    UpdateHandler(
        std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
        std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry> registry,
        std::shared_ptr<sea::application::AuthService> auth_service,
        std::string entity_name,
        std::shared_ptr<IBlockingExecutor> blocking_executor,
        // helper ABAC resource-aware (optionnel)
        std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper = nullptr
        );

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override;

private:
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine_;
    std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry> registry_;
    std::shared_ptr<sea::application::AuthService> auth_service_;
    std::string entity_name_;
    std::shared_ptr<IBlockingExecutor> blocking_executor_;
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper_;
};

}