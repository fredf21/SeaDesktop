#pragma once

#include <seastar/http/handlers.hh>
#include <seastar/http/httpd.hh>

#include <memory>
#include <string>

namespace sea::http::handlers::misc {

/**
 * @brief Wrapper handler qui exige le role admin avant de deleguer.
 *
 * Utilise pour proteger des routes systeme (OpenAPI, Swagger UI) qui
 * doivent rester accessibles en dev (auth=none) mais devenir reservees
 * aux admins en production (auth activee).
 *
 * Flow :
 *   1. Lit le header X-User-Role injecte par ProtectedHandler
 *   2. Si absent ou vide -> 401 Unauthorized
 *   3. Si different de admin_role_name (configure dans le YAML
 *      via authorization.admin_role) -> 403 Forbidden
 *   4. Sinon delegue au handler interne
 *
 * Le check est de la meme nature que celui de logs_handler.cpp :
 * la route est wrappee par ProtectedHandler (requires_auth=true) qui
 * verifie le JWT et injecte X-User-Role. AdminGuardHandler verifie
 * ensuite que ce role est bien celui de l'admin.
 *
 * Usage :
 *   auto inner = std::make_unique<OpenApiHandler>(openapi_json);
 *   auto guarded = std::make_unique<AdminGuardHandler>(
 *       std::move(inner),
 *       admin_role_name
 *   );
 *   auto wrapped = wrap_with_middlewares(std::move(guarded), true, ctx);
 */
class AdminGuardHandler final : public seastar::httpd::handler_base {
public:
    AdminGuardHandler(
        std::unique_ptr<seastar::httpd::handler_base> inner,
        std::string admin_role_name);

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring& path,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override;

private:
    std::unique_ptr<seastar::httpd::handler_base> _inner;
    std::string _admin_role_name;
};

} // namespace sea::http::handlers::misc