#include "protected_handler.h"
#include "../../utils/http_utils.h"

#include "authservice.h"

#include <nlohmann/json.hpp>
#include <utility>

namespace sea::http::handlers::auth {

using json = nlohmann::json;

ProtectedHandler::ProtectedHandler(
    std::unique_ptr<seastar::httpd::handler_base> inner,
    std::shared_ptr<sea::application::AuthService> auth_service)
    : inner_(std::move(inner))
    , auth_service_(std::move(auth_service))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
ProtectedHandler::handle(const seastar::sstring& path,
                         std::unique_ptr<seastar::http::request> req,
                         std::unique_ptr<seastar::http::reply> rep)
{
    const auto token = sea::http::utils::extract_bearer_token(*req);
    if (!token.has_value()) {
        rep->set_status(seastar::http::reply::status_type::unauthorized);
        rep->write_body("application/json", json{{"error", "Token manquant."}}.dump());
        co_return std::move(rep);
    }

    const auto claims = auth_service_->verify_token(*token);
    if (!claims.has_value()) {
        rep->set_status(seastar::http::reply::status_type::unauthorized);
        rep->write_body("application/json", json{{"error", "Token invalide."}}.dump());
        co_return std::move(rep);
    }

    co_return co_await inner_->handle(path, std::move(req), std::move(rep));
}

seastar::httpd::handler_base* maybe_protect_handler(
    std::unique_ptr<seastar::httpd::handler_base> handler,
    bool requires_auth,
    const std::shared_ptr<sea::application::AuthService>& auth_service)
{
    if (requires_auth) {
        return new ProtectedHandler(std::move(handler), auth_service);
    }

    return handler.release();
}

} // namespace sea::http::handlers::auth
