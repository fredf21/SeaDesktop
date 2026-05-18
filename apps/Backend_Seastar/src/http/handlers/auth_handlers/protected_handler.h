#pragma once
#include "authservice.h"
#include "thread_pool_execution/i_blocking_executor.h"
#include "security_scheme/cookie_config.h"

#include <seastar/http/httpd.hh>
#include <memory>

namespace sea::application { class AuthService; }
namespace sea::application::auth { class TokenTrackingService; }

namespace sea::http::handlers::auth {

/**
 * ProtectedHandler — Etape 1.4 (JWT-cookies + token tracking)
 *
 * Wrapper de securite pour proteger une route.
 *
 * Role :
 *  - extraire le token JWT depuis :
 *     1) header Authorization: Bearer (priorite)
 *     2) cookie sea_access (fallback, pour navigateur)
 *  - verifier sa signature/exp via JwtService
 *  - verifier qu'il n'est PAS revoque via TokenTrackingService::is_access_revoked
 *  - sinon refuser
 *  - injecter les claims dans X-User-* et passer au handler interne
 */
class ProtectedHandler final : public seastar::httpd::handler_base {
public:
    ProtectedHandler(
        std::unique_ptr<seastar::httpd::handler_base> inner,
        std::shared_ptr<sea::application::AuthService> auth_service,
        std::shared_ptr<sea::application::auth::TokenTrackingService> token_tracking,
        std::shared_ptr<IBlockingExecutor> blocking_executor,
        sea::domain::security::CookieConfig cookie_config
        );

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring& path,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override;

private:
    std::unique_ptr<seastar::httpd::handler_base>                 inner_;
    std::shared_ptr<sea::application::AuthService>                auth_service_;
    std::shared_ptr<sea::application::auth::TokenTrackingService> token_tracking_;
    std::shared_ptr<IBlockingExecutor>                            blocking_executor_;
    sea::domain::security::CookieConfig                           cookie_config_;

    void strip_user_headers(seastar::http::request& req) const;

    void inject_claims_as_headers(
        seastar::http::request& req,
        const sea::application::AuthUserClaims& claims) const;

    static std::string to_header_case(const std::string& claim_name);
};

/**
 * Helper : protege conditionnellement une route.
 * Si requires_auth = false, retourne le handler tel quel.
 */
std::unique_ptr<seastar::httpd::handler_base> maybe_protect(
    std::unique_ptr<seastar::httpd::handler_base> handler,
    bool requires_auth,
    const std::shared_ptr<sea::application::AuthService>& auth_service,
    const std::shared_ptr<sea::application::auth::TokenTrackingService>& token_tracking,
    const std::shared_ptr<IBlockingExecutor>& blocking_executor,
    const sea::domain::security::CookieConfig& cookie_config
    );

} // namespace sea::http::handlers::auth