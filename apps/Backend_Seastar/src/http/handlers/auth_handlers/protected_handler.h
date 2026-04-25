#pragma once
#include <seastar/http/httpd.hh>
#include <memory>

namespace sea::application { class AuthService; }

namespace sea::http::handlers::auth {

class ProtectedHandler final : public seastar::httpd::handler_base {
public:
    ProtectedHandler(std::unique_ptr<seastar::httpd::handler_base> inner,
                     std::shared_ptr<sea::application::AuthService> auth_service);

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring& path,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override;

private:
    std::unique_ptr<seastar::httpd::handler_base> inner_;
    std::shared_ptr<sea::application::AuthService> auth_service_;
};

seastar::httpd::handler_base* maybe_protect_handler(
    std::unique_ptr<seastar::httpd::handler_base> handler,
    bool requires_auth,
    const std::shared_ptr<sea::application::AuthService>& auth_service);

}
