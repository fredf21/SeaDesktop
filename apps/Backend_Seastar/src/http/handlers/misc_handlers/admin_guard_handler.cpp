#include "admin_guard_handler.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace sea::http::handlers::misc {

using nlohmann::json;

AdminGuardHandler::AdminGuardHandler(
    std::unique_ptr<seastar::httpd::handler_base> inner,
    std::string admin_role_name)
    : _inner(std::move(inner)),
    _admin_role_name(std::move(admin_role_name))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
AdminGuardHandler::handle(
    const seastar::sstring& path,
    std::unique_ptr<seastar::http::request> req,
    std::unique_ptr<seastar::http::reply> rep)
{
    // Recupere le role de l'utilisateur authentifie (injecte par
    // ProtectedHandler en amont via le header X-User-Role).
    const auto role_it = req->_headers.find("X-User-Role");

    // 401 Unauthorized : header absent ou vide => pas authentifie.
    // (Normalement impossible si la route est correctement wrappee
    // par ProtectedHandler avec requires_auth=true, mais on garde
    // une defense en profondeur au cas ou.)
    if (role_it == req->_headers.end() || role_it->second.empty()) {
        rep->set_status(seastar::http::reply::status_type::unauthorized);
        rep->write_body("application/json",
                        json{{"error", "Authentication required"}}.dump());
        return seastar::make_ready_future<std::unique_ptr<seastar::http::reply>>(
            std::move(rep));
    }

    // 403 Forbidden : authentifie mais pas avec le bon role.
    const std::string_view role_view(
        role_it->second.data(),
        role_it->second.size()
        );
    if (role_view != _admin_role_name) {
        rep->set_status(seastar::http::reply::status_type::forbidden);
        rep->write_body("application/json",
                        json{{"error", "Admin role required"}}.dump());
        return seastar::make_ready_future<std::unique_ptr<seastar::http::reply>>(
            std::move(rep));
    }

    // OK : delegue au handler interne.
    return _inner->handle(path, std::move(req), std::move(rep));
}

} // namespace sea::http::handlers::misc