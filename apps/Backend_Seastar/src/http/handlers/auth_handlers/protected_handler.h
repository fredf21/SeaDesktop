#pragma once
#include "thread_pool_execution/i_blocking_executor.h"
#include <seastar/http/httpd.hh>
#include <memory>

namespace sea::application { class AuthService; }

namespace sea::http::handlers::auth {

class ProtectedHandler final : public seastar::httpd::handler_base {
public:
    ProtectedHandler(std::unique_ptr<seastar::httpd::handler_base> inner,
                     std::shared_ptr<sea::application::AuthService> auth_service, std::shared_ptr<IBlockingExecutor> blocking_executor);

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring& path,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override;

private:
    std::unique_ptr<seastar::httpd::handler_base> inner_;
    std::shared_ptr<sea::application::AuthService> auth_service_;
    std::shared_ptr<IBlockingExecutor> blocking_executor_;
};

std::unique_ptr<seastar::httpd::handler_base> maybe_protect(
    std::unique_ptr<seastar::httpd::handler_base> handler,
    bool requires_auth,
    const std::shared_ptr<sea::application::AuthService>& auth_service, const std::shared_ptr<IBlockingExecutor>& blocking_executor );

}
