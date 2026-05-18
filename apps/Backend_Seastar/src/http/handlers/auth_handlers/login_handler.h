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
 * LoginHandler
 *
 * Route : POST /auth/login
 *
 * Etapes 1.4 (JWT-cookies + token tracking) :
 * - Apres generation des tokens, enregistre le refresh_jti dans
 *   l'allowlist (table RefreshToken) via TokenTrackingService
 * - Selon token_delivery (body / cookie / both), pose les Set-Cookie
 *   en plus (ou a la place) du body JSON
 *
 * Dependances injectees :
 *   - GenericCrudEngine   : lookup User
 *   - AuthService         : hash / verify password, generation JWT
 *   - TokenTrackingService: enregistrement refresh dans l'allowlist
 *   - IBlockingExecutor   : execution hors-reactor (bcrypt, libcrypto)
 *
 * Config :
 *   - CookieConfig        : attributs des cookies (path, secure, etc.)
 *   - TokenDelivery       : body | cookie | both
 *   - refresh_token_ttl   : Max-Age du cookie refresh
 *   - access_token_ttl    : Max-Age du cookie access
 */
class LoginHandler final : public seastar::httpd::handler_base {
public:
    LoginHandler(
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