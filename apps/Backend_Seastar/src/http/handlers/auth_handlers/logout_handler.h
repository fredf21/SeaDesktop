#pragma once

#include "thread_pool_execution/i_blocking_executor.h"
#include "security_scheme/cookie_config.h"

#include <seastar/http/httpd.hh>
#include <memory>

namespace sea::application {
class AuthService;
}

namespace sea::application::auth {
class TokenTrackingService;
}

namespace sea::http::handlers::auth {

/**
 * LogoutHandler
 *
 * Route : POST /auth/logout
 *
 * Etape 1.4 (JWT-cookies + token tracking).
 *
 * Flow :
 *  1. Extrait l'access_token depuis Authorization: Bearer OU cookie sea_access
 *  2. Extrait le refresh_token depuis body OU cookie sea_refresh (optionnel)
 *  3. Verifie les tokens pour extraire les jti
 *  4. Revoke l'access (denylist) + le refresh (allowlist)
 *  5. Pose des Set-Cookie a Max-Age=0 pour effacer les cookies cote navigateur
 *  6. Retourne 200 OK
 *
 * Idempotent : un appel sur un user deja deconnecte renvoie 200 OK
 * (les revocations sont des no-op si les tokens sont absents/invalides).
 *
 * Securite : on accepte que l'access ou le refresh manque (degrade
 * gracieusement), du moment qu'au moins un des deux est valide.
 */
class LogoutHandler final : public seastar::httpd::handler_base {
public:
    LogoutHandler(
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
    std::shared_ptr<sea::application::AuthService>                auth_service_;
    std::shared_ptr<sea::application::auth::TokenTrackingService> token_tracking_;
    std::shared_ptr<IBlockingExecutor>                            blocking_executor_;
    sea::domain::security::CookieConfig                           cookie_config_;
};

} // namespace sea::http::handlers::auth