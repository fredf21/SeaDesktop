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

namespace sea::infrastructure::runtime {
class GenericCrudEngine;
}

namespace sea::http::handlers::auth {

/**
 * RefreshHandler
 *
 * Route : POST /auth/refresh
 *
 * Etape 1.4 (JWT-cookies + token tracking).
 *
 * Flow :
 *  1. Lit le refresh_token depuis le body JSON OU le cookie sea_refresh
 *  2. Verifie la signature/exp via JwtService (synchrone, peu couteux)
 *  3. Verifie l'allowlist via TokenTrackingService::is_refresh_valid(jti)
 *  4. Recupere l'utilisateur pour reconstruire les claims (role, ABAC)
 *  5. Genere un nouveau access_token
 *  6. Si rotation activee : rotate_refresh() pour produire un nouveau refresh
 *     Sinon : reutilise l'ancien refresh
 *  7. Selon token_delivery, retourne body + Set-Cookie
 *
 * Securite :
 *  - Si l'allowlist refuse (revoked / expired / not found) -> 401
 *  - Si la signature est invalide -> 401
 *  - On NE renvoie PAS le refresh_token dans le body si token_delivery=cookie
 *    pour eviter qu'il fuite cote JS
 */
class RefreshHandler final : public seastar::httpd::handler_base {
public:
    RefreshHandler(
        std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
        std::shared_ptr<sea::application::AuthService> auth_service,
        std::shared_ptr<sea::application::auth::TokenTrackingService> token_tracking,
        std::shared_ptr<IBlockingExecutor> blocking_executor,
        sea::domain::security::CookieConfig cookie_config,
        sea::domain::security::TokenDelivery token_delivery,
        std::chrono::seconds access_token_ttl,
        std::chrono::seconds refresh_token_ttl
        );

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring& path,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override;

private:
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>  crud_engine_;
    std::shared_ptr<sea::application::AuthService>                    auth_service_;
    std::shared_ptr<sea::application::auth::TokenTrackingService>     token_tracking_;
    std::shared_ptr<IBlockingExecutor>                                blocking_executor_;
    sea::domain::security::CookieConfig                               cookie_config_;
    sea::domain::security::TokenDelivery                              token_delivery_;
    std::chrono::seconds                                              access_token_ttl_;
    std::chrono::seconds                                              refresh_token_ttl_;
};

} // namespace sea::http::handlers::auth