#include "refresh_handler.h"
#include "../../utils/http_utils.h"
#include "../../utils/cookie_helper.h"
#include "../../errors/error_response_factory.h"

#include "authservice.h"
#include "token_tracking_service.h"
#include "runtime/generic_crud_engine.h"
#include "security/jwt_service.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <set>
#include <utility>

namespace sea::http::handlers::auth {

using json = nlohmann::json;
namespace errors = sea::http::errors;
using Status = seastar::http::reply::status_type;

namespace {

// ─────────────────────────────────────────────────────────────────────
// Helper interne pour extraire l'IP client (cf. login/register/logout).
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

/**
 * Logs (sea.security) :
 * - info  refresh_success                          -> user_id, ip, rotation
 * - warn  refresh_failed reason='token_missing'    -> ip
 * - warn  refresh_failed reason='invalid_jwt'      -> ip
 * - warn  refresh_failed reason='not_in_allowlist' -> jti, ip (CRITIQUE :
 *         tente de reutiliser un token revoque ; possible vol de cookie)
 * - warn  refresh_failed reason='user_not_found'   -> user_id, ip
 * - error refresh_failed reason='rotation_failed'  -> user_id, jti
 */
seastar::future<std::unique_ptr<seastar::http::reply>>
RefreshHandler::handle(const seastar::sstring&,
                       std::unique_ptr<seastar::http::request> req,
                       std::unique_ptr<seastar::http::reply> rep)
{
    using sea::domain::security::TokenDelivery;
    namespace cookie_helper = sea::http::utils;

    auto sec_log = spdlog::get("sea.security");
    const std::string client_ip = extract_client_ip(*req);

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
            sec_log->warn(
                "refresh_failed: reason='token_missing' ip='{}'",
                client_ip);
            co_return errors::make_error_reply(
                Status::bad_request, "BAD_REQUEST",
                "refresh_token manquant.");
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
            // Token invalide cote signature/exp : peut etre normal (token
            // expire) ou suspect (token forge). On logue dans les 2 cas.
            sec_log->warn(
                "refresh_failed: reason='invalid_jwt' ip='{}'",
                client_ip);
            co_return errors::make_error_reply(
                Status::unauthorized, "AUTHENTICATION_ERROR",
                "refresh_token invalide.");
        }

        const std::string user_id     = jwt_claims->user_id;
        const std::string refresh_jti = jwt_claims->jti;

        if (user_id.empty()) {
            sec_log->warn(
                "refresh_failed: reason='token_incomplete' jti='{}' ip='{}'",
                refresh_jti, client_ip);
            co_return errors::make_error_reply(
                Status::unauthorized, "AUTHENTICATION_ERROR",
                "refresh_token incomplet.");
        }

        // ─── 4. Verification allowlist (tracking) ────────────────────
        if (token_tracking_ && !refresh_jti.empty()) {
            const bool valid = co_await token_tracking_->is_refresh_valid(refresh_jti);
            if (!valid) {
                // CRITIQUE : token cryptographiquement valide MAIS pas
                // dans l'allowlist. Cas typiques :
                //   - Token deja revoque (logout)
                //   - Token deja utilise puis rotate (apres rotation, le
                //     vieux n'est plus valide)
                //   - Vol de cookie : l'attaquant tente d'utiliser un
                //     vieux token capture
                // Tous ces cas meritent un warn pour analyse.
                sec_log->warn(
                    "refresh_failed: reason='not_in_allowlist' "
                    "user_id='{}' jti='{}' ip='{}'",
                    user_id, refresh_jti, client_ip);
                co_return errors::make_error_reply(
                    Status::unauthorized, "AUTHENTICATION_ERROR",
                    "refresh_token revoque ou expire.");
            }
        }

        // ─── 5. Recuperation de l'utilisateur (pour les claims) ──────
        const auto user_record =
            co_await crud_engine_->get_by_id("User", user_id);
        if (!user_record.has_value()) {
            // Token valide mais user supprime entre temps : log pour
            // detection d'incoherence.
            sec_log->warn(
                "refresh_failed: reason='user_not_found' user_id='{}' ip='{}'",
                user_id, client_ip);
            co_return errors::make_error_reply(
                Status::unauthorized, "AUTHENTICATION_ERROR",
                "Utilisateur introuvable.");
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
                // Erreur DB pendant la rotation atomique : cas suspect,
                // ne devrait pas arriver. Niveau error pour visibilite.
                sec_log->error(
                    "refresh_failed: reason='rotation_failed' "
                    "user_id='{}' jti='{}'",
                    user_id, refresh_jti);
                co_return errors::make_error_reply(
                    Status::unauthorized, "AUTHENTICATION_ERROR",
                    "Rotation du refresh impossible.");
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

        rep->set_status(Status::ok);
        rep->write_body("application/json", response_body.dump());

        // Log succes (apres construction de la reponse, juste avant
        // co_return).
        sec_log->info(
            "refresh_success: user_id='{}' ip='{}' rotation={} "
            "old_jti='{}' new_jti='{}'",
            user_id, client_ip,
            rotation_enabled ? "true" : "false",
            refresh_jti,
            rotation_enabled ? new_refresh_jti : refresh_jti);

        co_return std::move(rep);

    } catch (const errors::HttpException& e) {
        // Erreur metier deja typee : on respecte son statut.
        co_return errors::make_error_reply(e);
    } catch (const std::exception& e) {
        // Erreur generique : probable JSON malforme.
        spdlog::get("sea.http")->warn(
            "RefreshHandler: unhandled exception: {}", e.what());
        co_return errors::make_error_reply(
            Status::bad_request, "BAD_REQUEST",
            "Requete invalide.");
    } catch (...) {
        // Catch-all final.
        spdlog::get("sea.http")->error("RefreshHandler: unknown exception");
        co_return errors::make_internal_error_reply();
    }
}

} // namespace sea::http::handlers::auth