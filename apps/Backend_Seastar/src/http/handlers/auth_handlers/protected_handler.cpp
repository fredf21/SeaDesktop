#include "protected_handler.h"
#include "../../utils/http_utils.h"
#include "../../utils/cookie_helper.h"
#include "../../errors/error_response_factory.h"

#include "authservice.h"
#include "token_tracking_service.h"

#include <nlohmann/json.hpp>
#include <utility>
#include "security/jwt_service.h"

namespace sea::http::handlers::auth {

using json = nlohmann::json;
namespace errors = sea::http::errors;
using Status = seastar::http::reply::status_type;

namespace {

/**
 * Extrait l'access_token depuis :
 *   1) Header Authorization: Bearer <token>   (priorite)
 *   2) Cookie sea_access (fallback navigateur)
 */
std::optional<std::string> extract_access_token(
    const seastar::http::request& req,
    const std::string& cookie_name)
{
    // 1) Header Authorization
    if (auto tok = sea::http::utils::extract_bearer_token(req); tok.has_value()) {
        return tok;
    }
    // 2) Cookie
    if (const auto cookie_it = req._headers.find("Cookie");
        cookie_it != req._headers.end()) {
        const std::string_view cookie_header(
            cookie_it->second.data(), cookie_it->second.size()
            );
        if (auto v = sea::http::utils::get_cookie_value(cookie_header, cookie_name);
            v.has_value() && !v->empty()) {
            return v;
        }
    }
    return std::nullopt;
}

} // namespace anonyme

ProtectedHandler::ProtectedHandler(
    std::unique_ptr<seastar::httpd::handler_base> inner,
    std::shared_ptr<sea::application::AuthService> auth_service,
    std::shared_ptr<sea::application::auth::TokenTrackingService> token_tracking,
    std::shared_ptr<IBlockingExecutor> blocking_executor,
    sea::domain::security::CookieConfig cookie_config)
    : inner_(std::move(inner))
    , auth_service_(std::move(auth_service))
    , token_tracking_(std::move(token_tracking))
    , blocking_executor_(std::move(blocking_executor))
    , cookie_config_(std::move(cookie_config))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
ProtectedHandler::handle(const seastar::sstring& path,
                         std::unique_ptr<seastar::http::request> req,
                         std::unique_ptr<seastar::http::reply> rep)
{
    // ─── 1. Extraction token : Bearer prioritaire, cookie fallback ──
    const auto token = extract_access_token(
        *req, cookie_config_.access_token_name()
        );
    if (!token.has_value() || token->empty()) {
        co_return errors::make_error_reply(
            Status::unauthorized, "AUTHENTICATION_ERROR",
            "Token manquant.");
    }

    // ─── 2. Verification signature / exp (hors reactor) ─────────────
    const auto claims =
        co_await auth_service_->verify_token_async(*token, *blocking_executor_);

    if (!claims.has_value()) {
        co_return errors::make_error_reply(
            Status::unauthorized, "AUTHENTICATION_ERROR",
            "Token invalide.");
    }

    // ─── 3. Verification denylist (token tracking) ──────────────────
    // verify_token_async retourne un AuthUserClaims qui n'inclut PAS
    // le jti (l'API de AuthUserClaims expose user_id/email/role).
    // On extrait le jti via JwtService::verify_token (synchronous).
    // C'est rapide (signature deja verifiee, on extrait juste les claims).
    if (token_tracking_ && token_tracking_->config().is_enabled()) {
        using namespace domain::security;
        const auto verify_params = infrastructure::security::VerifyTokenParams{
            .token           = *token,
            .secret          = auth_service_->config().jwt_secret(),
            .expected_issuer = auth_service_->issuer(),
            .expected_type   = infrastructure::security::TokenType::Access
        };
        const auto raw_claims = infrastructure::security::JwtService::verify_token(verify_params);

        if (raw_claims.has_value() && !raw_claims->jti.empty()) {
            const bool revoked = co_await token_tracking_->is_access_revoked(
                raw_claims->jti
                );
            if (revoked) {
                co_return errors::make_error_reply(
                    Status::unauthorized, "AUTHENTICATION_ERROR",
                    "Token revoque.");
            }
        }
        // Si raw_claims->jti est vide, on accepte (compat tokens pre-1.3).
    }

    // ─── 4. Injection des claims comme X-User-* ─────────────────────
    strip_user_headers(*req);
    inject_claims_as_headers(*req, *claims);

    // ─── 5. Delegue au handler interne ──────────────────────────────
    co_return co_await inner_->handle(path, std::move(req), std::move(rep));
}

void ProtectedHandler::strip_user_headers(seastar::http::request& req) const
{
    std::vector<seastar::sstring> to_remove;
    to_remove.reserve(8);

    for (const auto& kv : req._headers) {
        const auto& key = kv.first;
        if (key.size() < 7) continue;

        bool matches = true;
        static constexpr char prefix[] = "x-user-";
        for (std::size_t i = 0; i < 7; ++i) {
            const char c = static_cast<char>(
                std::tolower(static_cast<unsigned char>(key[i])));
            if (c != prefix[i]) { matches = false; break; }
        }
        if (matches) to_remove.push_back(key);
    }
    for (const auto& key : to_remove) {
        req._headers.erase(key);
    }
}

void ProtectedHandler::inject_claims_as_headers(
    seastar::http::request& req,
    const sea::application::AuthUserClaims& claims) const
{
    if (!claims.user_id.empty()) req._headers["X-User-Id"]    = claims.user_id;
    if (!claims.email.empty())   req._headers["X-User-Email"] = claims.email;
    if (!claims.role.empty())    req._headers["X-User-Role"]  = claims.role;

    for (const auto& [key, value] : claims.additional_claims) {
        if (key.empty() || value.empty()) continue;
        const std::string header_name = "X-User-" + to_header_case(key);
        req._headers[header_name] = value;
    }
}

std::string ProtectedHandler::to_header_case(const std::string& claim_name)
{
    std::string result;
    result.reserve(claim_name.size());
    bool capitalize_next = true;

    for (char c : claim_name) {
        if (c == '_' || c == '-') {
            result += '-';
            capitalize_next = true;
        } else if (capitalize_next) {
            result += static_cast<char>(
                std::toupper(static_cast<unsigned char>(c)));
            capitalize_next = false;
        } else {
            result += c;
        }
    }
    return result;
}

std::unique_ptr<seastar::httpd::handler_base> maybe_protect(
    std::unique_ptr<seastar::httpd::handler_base> handler,
    bool requires_auth,
    const std::shared_ptr<sea::application::AuthService>& auth_service,
    const std::shared_ptr<sea::application::auth::TokenTrackingService>& token_tracking,
    const std::shared_ptr<IBlockingExecutor>& blocking_executor,
    const sea::domain::security::CookieConfig& cookie_config)
{
    if (!requires_auth) {
        return handler;
    }
    return std::make_unique<ProtectedHandler>(
        std::move(handler),
        auth_service,
        token_tracking,
        blocking_executor,
        cookie_config
        );
}

} // namespace sea::http::handlers::auth