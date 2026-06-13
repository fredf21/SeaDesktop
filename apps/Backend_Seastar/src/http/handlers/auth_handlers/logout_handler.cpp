#include "logout_handler.h"
#include "../../utils/http_utils.h"
#include "../../utils/cookie_helper.h"

#include "authservice.h"
#include "token_tracking_service.h"
#include "security/jwt_service.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <utility>

namespace sea::http::handlers::auth {

using json = nlohmann::json;

namespace {

// ─────────────────────────────────────────────────────────────────────
// Helper interne pour extraire l'IP client (cf. login/register).
// ─────────────────────────────────────────────────────────────────────
std::string extract_client_ip(const seastar::http::request& req)
{
    if (const auto it = req._headers.find("X-Forwarded-For");
        it != req._headers.end()) {
        std::string value(it->second.data(), it->second.size());
        const auto comma = value.find(',');
        if (comma != std::string::npos) {
            value.resize(comma);
        }
        while (!value.empty() && value.front() == ' ') value.erase(0, 1);
        while (!value.empty() && value.back() == ' ') value.pop_back();
        return value;
    }
    if (const auto it = req._headers.find("X-Real-IP");
        it != req._headers.end()) {
        return std::string(it->second.data(), it->second.size());
    }
    return {};
}

/**
 * Extrait l'access_token depuis :
 *   1) Header Authorization: Bearer <token>
 *   2) Cookie sea_access (fallback)
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
        return sea::http::utils::get_cookie_value(cookie_header, cookie_name);
    }
    return std::nullopt;
}

/**
 * Extrait le refresh_token depuis body OU cookie.
 */
std::optional<std::string> extract_refresh_token(
    const seastar::http::request& req,
    const std::string& cookie_name,
    const std::string& body_str)
{
    if (!body_str.empty()) {
        try {
            const auto body = nlohmann::json::parse(body_str);
            if (body.contains("refresh_token") && body["refresh_token"].is_string()) {
                const auto v = body["refresh_token"].get<std::string>();
                if (!v.empty()) return v;
            }
        } catch (...) {}
    }
    if (const auto cookie_it = req._headers.find("Cookie");
        cookie_it != req._headers.end()) {
        const std::string_view cookie_header(
            cookie_it->second.data(), cookie_it->second.size()
            );
        return sea::http::utils::get_cookie_value(cookie_header, cookie_name);
    }
    return std::nullopt;
}

} // namespace anonyme

LogoutHandler::LogoutHandler(
    std::shared_ptr<sea::application::AuthService> auth_service,
    std::shared_ptr<sea::application::auth::TokenTrackingService> token_tracking,
    std::shared_ptr<IBlockingExecutor> blocking_executor,
    sea::domain::security::CookieConfig cookie_config)
    : auth_service_(std::move(auth_service))
    , token_tracking_(std::move(token_tracking))
    , blocking_executor_(std::move(blocking_executor))
    , cookie_config_(std::move(cookie_config))
{
}

/**
 * Logs (sea.security) :
 * - info logout: avec details du token revoque (user_id, jti, ip)
 * - info logout: anonymous quand aucun token valide n'est trouve
 */
seastar::future<std::unique_ptr<seastar::http::reply>>
LogoutHandler::handle(const seastar::sstring&,
                      std::unique_ptr<seastar::http::request> req,
                      std::unique_ptr<seastar::http::reply> rep)
{
    namespace cookie_helper = sea::http::utils;

    auto sec_log = spdlog::get("sea.security");
    const std::string client_ip = extract_client_ip(*req);

    // Variables pour le log final : on les remplit a mesure qu'on
    // identifie le user/jti revoque.
    std::string logged_user_id;
    std::string revoked_access_jti;
    std::string revoked_refresh_jti;

    // ─── 1. Lecture body (optionnel pour logout) ────────────────
    std::string body_str;
    try {
        body_str = co_await sea::http::utils::read_request_body(*req);
    } catch (...) {
        body_str = "";
    }

    // ─── 2. Extraction des tokens ───────────────────────────────
    const auto access_opt = extract_access_token(
        *req, cookie_config_.access_token_name()
        );
    const auto refresh_opt = extract_refresh_token(
        *req, cookie_config_.refresh_token_name(), body_str
        );

    // ─── 3. Revoke access (denylist) ────────────────────────────
    if (token_tracking_ && access_opt.has_value() && !access_opt->empty()) {
        using namespace sea::infrastructure::security;
        const auto verify_params = VerifyTokenParams{
            .token           = *access_opt,
            .secret          = auth_service_->config().jwt_secret(),
            .expected_issuer = auth_service_->issuer(),
            .expected_type   = TokenType::Access
        };
        const auto claims = JwtService::verify_token(verify_params);

        // Si le token est encore valide et a un jti, on le revoke.
        // Sinon (expire/invalide), pas besoin -- il est deja inactif.
        const auto exp_time_point =
            std::chrono::system_clock::from_time_t(static_cast<std::time_t>(claims->expires_at));
        if (claims.has_value() && !claims->jti.empty()) {
            co_await token_tracking_->revoke_access(
                claims->jti,
                claims->user_id,
                exp_time_point,
                "logout"
                );
            logged_user_id = claims->user_id;
            revoked_access_jti = claims->jti;
        }
    }

    // ─── 4. Revoke refresh (allowlist) ──────────────────────────
    if (token_tracking_ && refresh_opt.has_value() && !refresh_opt->empty()) {
        using namespace sea::infrastructure::security;
        const auto verify_params = VerifyTokenParams{
            .token           = *refresh_opt,
            .secret          = auth_service_->config().jwt_secret(),
            .expected_issuer = auth_service_->issuer(),
            .expected_type   = TokenType::Refresh
        };
        const auto claims = JwtService::verify_token(verify_params);

        if (claims.has_value() && !claims->jti.empty()) {
            co_await token_tracking_->revoke_refresh(claims->jti);
            // user_id peut ne pas avoir ete capture si l'access etait
            // expire/invalide : on le complete depuis le refresh.
            if (logged_user_id.empty()) {
                logged_user_id = claims->user_id;
            }
            revoked_refresh_jti = claims->jti;
        }
    }

    // ─── 5. Clear cookies (toujours, meme si pas de tokens) ─────
    rep->add_header(
        "Set-Cookie",
        cookie_helper::clear_access_cookie(cookie_config_)
        );
    rep->add_header(
        "Set-Cookie",
        cookie_helper::clear_refresh_cookie(cookie_config_)
        );

    // ─── 6. Reponse ─────────────────────────────────────────────
    rep->set_status(seastar::http::reply::status_type::ok);
    rep->write_body("application/json",
                    json{{"message", "Deconnexion reussie"}}.dump());

    // ─── 7. Log securite ────────────────────────────────────────
    // Trois cas :
    //   - On a revoque au moins un token  -> log avec details
    //   - Aucun token valide              -> log anonyme
    if (!logged_user_id.empty()) {
        sec_log->info(
            "logout: user_id='{}' ip='{}' access_jti='{}' refresh_jti='{}'",
            logged_user_id,
            client_ip,
            revoked_access_jti,
            revoked_refresh_jti);
    } else {
        // Cas frequent : utilisateur deja deconnecte (cookies absents
        // ou tokens expires). Le client appelle quand meme /logout pour
        // forcer le clear des cookies. Pas une erreur, juste un signal.
        sec_log->info("logout: anonymous ip='{}'", client_ip);
    }

    co_return std::move(rep);
}

} // namespace sea::http::handlers::auth