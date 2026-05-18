#include "refresh_handler.h"
#include "../../utils/http_utils.h"
#include "../../utils/cookie_helper.h"

#include "authservice.h"
#include "token_tracking_service.h"
#include "runtime/generic_crud_engine.h"
#include "security/jwt_service.h"

#include <nlohmann/json.hpp>
#include <set>
#include <utility>

namespace sea::http::handlers::auth {

using json = nlohmann::json;

namespace {

/**
 * Lit le refresh_token depuis le body JSON ou le cookie configure.
 * Le body est prioritaire (compatible API/CLI), le cookie est fallback.
 */
std::optional<std::string> extract_refresh_token(
    const seastar::http::request& req,
    const std::string& cookie_name,
    const std::string& body_str)
{
    // 1) Tente le body JSON
    if (!body_str.empty()) {
        try {
            const auto body = nlohmann::json::parse(body_str);
            if (body.contains("refresh_token") && body["refresh_token"].is_string()) {
                const auto value = body["refresh_token"].get<std::string>();
                if (!value.empty()) return value;
            }
        } catch (...) {
            // body pas du JSON valide -> on tente le cookie
        }
    }

    // 2) Fallback : cookie
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

RefreshHandler::RefreshHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::shared_ptr<sea::application::AuthService> auth_service,
    std::shared_ptr<sea::application::auth::TokenTrackingService> token_tracking,
    std::shared_ptr<IBlockingExecutor> blocking_executor,
    sea::domain::security::CookieConfig cookie_config,
    sea::domain::security::TokenDelivery token_delivery,
    std::chrono::seconds access_token_ttl,
    std::chrono::seconds refresh_token_ttl)
    : crud_engine_(std::move(crud_engine))
    , auth_service_(std::move(auth_service))
    , token_tracking_(std::move(token_tracking))
    , blocking_executor_(std::move(blocking_executor))
    , cookie_config_(std::move(cookie_config))
    , token_delivery_(token_delivery)
    , access_token_ttl_(access_token_ttl)
    , refresh_token_ttl_(refresh_token_ttl)
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
RefreshHandler::handle(const seastar::sstring&,
                       std::unique_ptr<seastar::http::request> req,
                       std::unique_ptr<seastar::http::reply> rep)
{
    using sea::domain::security::TokenDelivery;
    namespace cookie_helper = sea::http::utils;

    try {
        // ─── 1. Lecture body (pour /refresh, le body est optionnel) ──
        std::string body_str;
        try {
            body_str = co_await sea::http::utils::read_request_body(*req);
        } catch (...) {
            body_str = "";   // pas grave, on tentera le cookie
        }

        // ─── 2. Extraction du refresh_token (body OU cookie) ────────
        const auto refresh_token_opt = extract_refresh_token(
            *req,
            cookie_config_.refresh_token_name(),
            body_str
            );
        if (!refresh_token_opt.has_value() || refresh_token_opt->empty()) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("application/json",
                            json{{"error", "refresh_token manquant"}}.dump());
            co_return std::move(rep);
        }
        const std::string& refresh_token = *refresh_token_opt;

        // ─── 3. Verification signature/exp du JWT ────────────────────
        using namespace sea::infrastructure::security;
        const auto verify_params = VerifyTokenParams{
            .token           = refresh_token,
            .secret          = auth_service_->config().jwt_secret(),
            .expected_issuer = auth_service_->issuer(),
            .expected_type   = TokenType::Refresh
        };
        const auto jwt_claims = JwtService::verify_token(verify_params);
        if (!jwt_claims.has_value()) {
            rep->set_status(seastar::http::reply::status_type::unauthorized);
            rep->write_body("application/json",
                            json{{"error", "refresh_token invalide"}}.dump());
            co_return std::move(rep);
        }

        const std::string user_id     = jwt_claims->user_id;
        const std::string refresh_jti = jwt_claims->jti;

        if (user_id.empty()) {
            rep->set_status(seastar::http::reply::status_type::unauthorized);
            rep->write_body("application/json",
                            json{{"error", "refresh_token incomplet"}}.dump());
            co_return std::move(rep);
        }

        // ─── 4. Verification allowlist (tracking) ────────────────────
        if (token_tracking_ && !refresh_jti.empty()) {
            const bool valid = co_await token_tracking_->is_refresh_valid(refresh_jti);
            if (!valid) {
                rep->set_status(seastar::http::reply::status_type::unauthorized);
                rep->write_body("application/json",
                                json{{"error", "refresh_token revoque ou expire"}}.dump());
                co_return std::move(rep);
            }
        }

        // ─── 5. Recuperation de l'utilisateur (pour les claims) ──────
        const auto user_record =
            co_await crud_engine_->get_by_id("User", user_id);
        if (!user_record.has_value()) {
            rep->set_status(seastar::http::reply::status_type::unauthorized);
            rep->write_body("application/json",
                            json{{"error", "Utilisateur introuvable"}}.dump());
            co_return std::move(rep);
        }

        // Email
        std::string email;
        if (const auto email_it = user_record->find("email");
            email_it != user_record->end()) {
            email = sea::http::utils::dynamic_value_to_string(email_it->second)
            .value_or("");
        }

        // Role
        std::string role = "user";
        if (const auto role_it = user_record->find("role");
            role_it != user_record->end()) {
            const auto rv = sea::http::utils::dynamic_value_to_string(role_it->second);
            if (rv.has_value() && !rv->empty()) role = *rv;
        }

        // Additional claims
        std::unordered_map<std::string, std::string> additional_claims;
        static const std::set<std::string> excluded_fields = {
            "id", "email", "role", "password", "full_name",
            "created_at", "updated_at", "deleted_at"
        };
        for (const auto& [field_name, field_value] : *user_record) {
            if (excluded_fields.count(field_name)) continue;
            const auto vs =
                sea::http::utils::dynamic_value_to_string(field_value);
            if (vs.has_value() && !vs->empty()) {
                additional_claims[field_name] = *vs;
            }
        }

        // ─── 6. Generation du nouveau access_token ───────────────────
        const auto new_access_token =
            co_await auth_service_->generate_access_token_async(
                user_id, email, role, additional_claims, *blocking_executor_
                );

        // ─── 7. Rotation du refresh (si activee) ─────────────────────
        std::string new_refresh_token = refresh_token;   // par defaut, on garde
        std::string new_refresh_jti   = refresh_jti;

        const bool rotation_enabled =
            token_tracking_ &&
            token_tracking_->config().is_enabled() &&
            token_tracking_->config().rotation().is_enabled();

        if (rotation_enabled) {
            // Genere le nouveau refresh
            new_refresh_token =
                co_await auth_service_->generate_refresh_token_async(
                    user_id, *blocking_executor_
                    );

            // Extrait son jti
            const auto new_verify_params = VerifyTokenParams{
                .token           = new_refresh_token,
                .secret          = auth_service_->config().jwt_secret(),
                .expected_issuer = auth_service_->issuer(),
                .expected_type   = TokenType::Refresh
            };
            const auto new_claims = JwtService::verify_token(new_verify_params);
            if (new_claims.has_value()) {
                new_refresh_jti = new_claims->jti;
            }

            // Device info pour audit
            std::string device_info;
            std::string ip_address;
            if (const auto ua_it = req->_headers.find("User-Agent");
                ua_it != req->_headers.end()) {
                device_info = std::string(ua_it->second.data(), ua_it->second.size());
            }
            if (const auto fwd_it = req->_headers.find("X-Forwarded-For");
                fwd_it != req->_headers.end()) {
                ip_address = std::string(fwd_it->second.data(), fwd_it->second.size());
            }

            // Atomic : revoke ancien + insert nouveau (in_transaction)
            const auto now = std::chrono::system_clock::now();
            const bool rotated = co_await token_tracking_->rotate_refresh(
                refresh_jti,
                new_refresh_jti,
                user_id,
                now,
                now + refresh_token_ttl_,
                device_info,
                ip_address
                );

            if (!rotated) {
                rep->set_status(seastar::http::reply::status_type::unauthorized);
                rep->write_body("application/json",
                                json{{"error", "Rotation du refresh impossible"}}.dump());
                co_return std::move(rep);
            }
        }

        // ─── 8. Reponse selon token_delivery ─────────────────────────
        const bool deliver_body =
            (token_delivery_ == TokenDelivery::Body) ||
            (token_delivery_ == TokenDelivery::Both);
        const bool deliver_cookie =
            (token_delivery_ == TokenDelivery::Cookie) ||
            (token_delivery_ == TokenDelivery::Both);

        json response_body;
        response_body["token_type"] = "Bearer";

        if (deliver_body) {
            response_body["access_token"]  = new_access_token;
            if (rotation_enabled) {
                response_body["refresh_token"] = new_refresh_token;
            }
        }

        if (deliver_cookie) {
            rep->add_header(
                "Set-Cookie",
                cookie_helper::build_access_cookie(
                    cookie_config_, new_access_token, access_token_ttl_
                    )
                );
            if (rotation_enabled) {
                rep->add_header(
                    "Set-Cookie",
                    cookie_helper::build_refresh_cookie(
                        cookie_config_, new_refresh_token, refresh_token_ttl_
                        )
                    );
            }
        }

        rep->set_status(seastar::http::reply::status_type::ok);
        rep->write_body("application/json", response_body.dump());
        co_return std::move(rep);

    } catch (...) {
        rep->set_status(seastar::http::reply::status_type::bad_request);
        rep->write_body("application/json",
                        json{{"error", "Requete invalide"}}.dump());
        co_return std::move(rep);
    }
}

} // namespace sea::http::handlers::auth