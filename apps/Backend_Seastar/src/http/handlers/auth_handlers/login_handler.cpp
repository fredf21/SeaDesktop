#include "login_handler.h"
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
// Helpers internes pour les logs sea.security.
// Extraction de l'IP client (X-Forwarded-For prioritaire, X-Real-IP
// fallback) et troncation du User-Agent pour ne pas noyer les logs.
// ─────────────────────────────────────────────────────────────────────

std::string extract_client_ip(const seastar::http::request& req)
{
    if (const auto it = req._headers.find("X-Forwarded-For");
        it != req._headers.end()) {
        // X-Forwarded-For peut etre "ip1, ip2, ip3" : on prend la 1ere
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

std::string extract_user_agent_short(const seastar::http::request& req)
{
    constexpr std::size_t MAX_LEN = 80;
    if (const auto it = req._headers.find("User-Agent");
        it != req._headers.end()) {
        std::string ua(it->second.data(), it->second.size());
        if (ua.size() > MAX_LEN) ua.resize(MAX_LEN);
        return ua;
    }
    return {};
}

} // namespace anonyme

LoginHandler::LoginHandler(
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
 * LoginHandler
 *
 * Route : POST /auth/login
 *
 * Flow :
 * 1. Lire body, parser JSON
 * 2. Verifier email/password (bcrypt async)
 * 3. Construire les claims (additional_claims pour ABAC)
 * 4. Generer access_token + refresh_token (JwtService produit le jti UUID v4)
 * 5. Enregistrer refresh_jti dans l'allowlist (TokenTrackingService)
 * 6. Selon token_delivery, poser les Set-Cookie + retourner body
 *
 * Logs (sea.security) :
 * - info  : login_success
 * - warn  : login_failed avec reason precise (user_not_found,
 *           password_missing, wrong_password)
 * - error : login_invalid_user_record (user sans id ou id mal type)
 */
seastar::future<std::unique_ptr<seastar::http::reply>>
LoginHandler::handle(const seastar::sstring&,
                     std::unique_ptr<seastar::http::request> req,
                     std::unique_ptr<seastar::http::reply> rep)
{
    using sea::domain::security::TokenDelivery;
    namespace cookie_helper = sea::http::utils;

    auto sec_log = spdlog::get("sea.security");
    const std::string client_ip = extract_client_ip(*req);

    try {
        // ─── 1. Lecture body ────────────────────────────────────
        const std::string reqbody =
            co_await sea::http::utils::read_request_body(*req);

        const auto body = json::parse(reqbody);

        if (!body.contains("email") || !body.contains("password")) {
            co_return errors::make_error_reply(
                Status::bad_request, "BAD_REQUEST",
                "email et password sont requis.");
        }

        const std::string email    = body["email"].get<std::string>();
        const std::string password = body["password"].get<std::string>();

        // ─── 2. Recherche utilisateur ───────────────────────────
        const auto user_record =
            co_await crud_engine_->find_one_by_field("User", "email", email);

        if (!user_record.has_value()) {
            sec_log->warn(
                "login_failed: reason='user_not_found' email='{}' ip='{}'",
                email, client_ip);
            co_return errors::make_error_reply(
                Status::unauthorized, "AUTHENTICATION_ERROR",
                "Identifiants invalides.");
        }

        // Recuperation hash password
        const auto pwd_it = user_record->find("password");
        if (pwd_it == user_record->end()) {
            sec_log->warn(
                "login_failed: reason='password_missing' email='{}' ip='{}'",
                email, client_ip);
            co_return errors::make_error_reply(
                Status::unauthorized, "AUTHENTICATION_ERROR",
                "Identifiants invalides.");
        }

        const auto stored_hash =
            sea::http::utils::dynamic_value_to_string(pwd_it->second);

        if (!stored_hash.has_value() || stored_hash->empty()) {
            sec_log->warn(
                "login_failed: reason='password_missing' email='{}' ip='{}'",
                email, client_ip);
            co_return errors::make_error_reply(
                Status::unauthorized, "AUTHENTICATION_ERROR",
                "Identifiants invalides.");
        }

        // ─── 3. Verification password (hors reactor) ────────────
        const bool password_ok =
            co_await blocking_executor_->submit(
                [auth_service = auth_service_,
                 password,
                 hash = *stored_hash]() {
                    return auth_service->verify_password(password, hash);
                }
                );

        if (!password_ok) {
            sec_log->warn(
                "login_failed: reason='wrong_password' email='{}' ip='{}'",
                email, client_ip);
            co_return errors::make_error_reply(
                Status::unauthorized, "AUTHENTICATION_ERROR",
                "Identifiants invalides.");
        }

        // ─── 4. Extraction user_id, role ────────────────────────
        const auto id_it = user_record->find("id");
        if (id_it == user_record->end()) {
            // Cas suspect : un User en base sans id, ne devrait pas
            // arriver. On logue pour investigation et retourne 500
            // generique (ne pas exposer ce detail interne au client).
            sec_log->error(
                "login_invalid_user_record: email='{}' issue='no_id_field'",
                email);
            spdlog::get("sea.http")->error(
                "LoginHandler: user found by email='{}' has no id field",
                email);
            co_return errors::make_internal_error_reply();
        }

        const auto user_id =
            sea::http::utils::dynamic_value_to_string_id(id_it->second);

        if (!user_id.has_value()) {
            // Pareil : id present mais inconvertible. Cas suspect.
            sec_log->error(
                "login_invalid_user_record: email='{}' issue='invalid_id_type'",
                email);
            spdlog::get("sea.http")->error(
                "LoginHandler: user (email='{}') has invalid id type",
                email);
            co_return errors::make_internal_error_reply();
        }

        std::string role = "user";
        if (const auto role_it = user_record->find("role"); role_it != user_record->end()) {
            const auto role_value =
                sea::http::utils::dynamic_value_to_string(role_it->second);
            if (role_value.has_value() && !role_value->empty()) {
                role = *role_value;
            }
        }

        // ─── 5. Additional claims pour ABAC ─────────────────────
        std::unordered_map<std::string, std::string> additional_claims;

        static const std::set<std::string> excluded_fields = {
            "id", "email", "role", "password", "full_name",
            "created_at", "updated_at", "deleted_at"
        };

        for (const auto& [field_name, field_value] : *user_record) {
            if (excluded_fields.count(field_name)) continue;
            const auto value_str =
                sea::http::utils::dynamic_value_to_string(field_value);
            if (value_str.has_value() && !value_str->empty()) {
                additional_claims[field_name] = *value_str;
            }
        }

        // ─── 6. Generation des tokens ───────────────────────────
        const auto access_token =
            co_await auth_service_->generate_access_token_async(
                *user_id, email, role, additional_claims, *blocking_executor_
                );

        const auto refresh_token =
            co_await auth_service_->generate_refresh_token_async(
                *user_id, *blocking_executor_
                );

        // ─── 7. Enregistrement du refresh dans l'allowlist ──────
        // Extraction du jti depuis le refresh token genere (le JwtService
        // a genere un UUID v4 si on n'a pas fourni de jti aux params)
        std::string refresh_jti;
        {
            using namespace sea::infrastructure::security;
            const auto verify_params = VerifyTokenParams{
                .token           = refresh_token,
                .secret          = auth_service_->config().jwt_secret(),
                .expected_issuer = auth_service_->issuer(),
                .expected_type   = TokenType::Refresh
            };
            const auto claims = JwtService::verify_token(verify_params);
            if (claims.has_value()) {
                refresh_jti = claims->jti;
            }
        }

        if (token_tracking_ && !refresh_jti.empty()) {
            // Capture device_info et IP pour audit
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

            const auto now = std::chrono::system_clock::now();
            co_await token_tracking_->register_refresh(
                refresh_jti,
                *user_id,
                now,
                now + refresh_token_ttl_,
                device_info,
                ip_address
                );
        }

        // ─── 8. Construction de la reponse ──────────────────────
        json user_json = json::parse(sea::http::utils::record_to_json(*user_record));
        user_json.erase("password");

        const bool deliver_body =
            (token_delivery_ == TokenDelivery::Body) ||
            (token_delivery_ == TokenDelivery::Both);
        const bool deliver_cookie =
            (token_delivery_ == TokenDelivery::Cookie) ||
            (token_delivery_ == TokenDelivery::Both);

        // Body JSON : tokens inclus si Body ou Both
        json response_body;
        response_body["user"]       = user_json;
        response_body["token_type"] = "Bearer";

        if (deliver_body) {
            response_body["access_token"]  = access_token;
            response_body["refresh_token"] = refresh_token;
        }

        // Set-Cookie : si Cookie ou Both
        if (deliver_cookie) {
            const auto access_cookie = cookie_helper::build_access_cookie(
                cookie_config_, access_token, access_token_ttl_
                );
            const auto refresh_cookie = cookie_helper::build_refresh_cookie(
                cookie_config_, refresh_token, refresh_token_ttl_
                );

            rep->add_header("Set-Cookie", access_cookie);
            rep->add_header("Set-Cookie", refresh_cookie);
        }

        rep->set_status(Status::ok);
        rep->write_body("application/json", response_body.dump());

        // Log succes : on logue user_id + email pour audit, role pour
        // visibilite, ip + device pour traque (forensics).
        sec_log->info(
            "login_success: user_id='{}' email='{}' role='{}' ip='{}' device='{}'",
            *user_id, email, role, client_ip,
            extract_user_agent_short(*req));

        co_return std::move(rep);

    } catch (const errors::HttpException& e) {
        // Erreur metier deja typee : on respecte son statut.
        co_return errors::make_error_reply(e);
    } catch (const std::exception& e) {
        // Erreur generique : probable JSON malforme ou champ manquant.
        // 400 avec message generique (ne pas exposer de details).
        spdlog::get("sea.http")->warn(
            "LoginHandler: unhandled exception: {}", e.what());
        co_return errors::make_error_reply(
            Status::bad_request, "BAD_REQUEST",
            "Requete invalide.");
    } catch (...) {
        // Catch-all final : rien ne doit echapper.
        spdlog::get("sea.http")->error("LoginHandler: unknown exception");
        co_return errors::make_internal_error_reply();
    }
}

} // namespace sea::http::handlers::auth