#pragma once
#include "thread_pool_execution/i_blocking_executor.h"
#include <seastar/http/httpd.hh>
#include <memory>

namespace sea::application { class AuthService; }
namespace sea::infrastructure::runtime { class GenericCrudEngine; }

namespace sea::http::handlers::auth {

class LoginHandler final : public seastar::httpd::handler_base {
public:
    LoginHandler(
        std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
        std::shared_ptr<sea::application::AuthService> auth_service,
        std::shared_ptr<IBlockingExecutor> blocking_executor);

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override;

private:
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine_;
    std::shared_ptr<sea::application::AuthService> auth_service_;
    std::shared_ptr<IBlockingExecutor> blocking_executor_;
};

}
