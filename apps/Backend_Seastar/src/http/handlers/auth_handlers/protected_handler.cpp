#include "protected_handler.h"
#include "../../utils/http_utils.h"

#include "authservice.h"

#include <nlohmann/json.hpp>
#include <utility>

namespace sea::http::handlers::auth {

using json = nlohmann::json;

/**
 * ProtectedHandler
 *
 * Wrapper de sécurité pour protéger une route.
 *
 * Rôle :
 * - vérifier le token JWT
 * - refuser si invalide
 * - sinon déléguer au handler réel
 */
ProtectedHandler::ProtectedHandler( std::unique_ptr<seastar::httpd::handler_base> inner,
                                   std::shared_ptr<sea::application::AuthService> auth_service, std::shared_ptr<IBlockingExecutor> blocking_executor)
    : inner_(std::move(inner))
    , auth_service_(std::move(auth_service))
    , blocking_executor_(std::move(blocking_executor))
{
}

/**
 * Interception de la requête HTTP
 */
seastar::future<std::unique_ptr<seastar::http::reply>>
ProtectedHandler::handle(const seastar::sstring& path,
                         std::unique_ptr<seastar::http::request> req,
                         std::unique_ptr<seastar::http::reply> rep)
{
    /**
     * Extraction token Bearer
     */
    const auto token = sea::http::utils::extract_bearer_token(*req);

    if (!token.has_value()) {
        rep->set_status(seastar::http::reply::status_type::unauthorized);
        rep->write_body("application/json",
                        json{{"error", "Token manquant"}}.dump());
        co_return std::move(rep);
    }

    /**
     * Vérification JWT
     *
     * NOTE :
     * Si verify_token devient CPU-heavy → déplacer dans blocking_executor
     */
    const auto claims =
        co_await auth_service_->verify_token_async(
            *token,
            *blocking_executor_
            );
    if (!claims.has_value()) {
        rep->set_status(seastar::http::reply::status_type::unauthorized);
        rep->write_body("application/json",
                        json{{"error", "Token invalide"}}.dump());
        co_return std::move(rep);
    }

    /**
     * Injection future possible :
     * → ajouter les claims dans req (context utilisateur)
     */

    /**
     * Injection des claims dans la requête.
     *
     * 1. SÉCURITÉ : strip tous les X-User-* qui pourraient venir du client
     *    (sinon un attaquant pourrait forger X-User-Role: admin)
     *
     * 2. Inject les vrais claims comme headers
     *    (lus par AuthorizationMiddleware pour construire PolicySubject)
     */
    strip_user_headers(*req);
    inject_claims_as_headers(*req, *claims);


    /**
     * Passage au handler réel
     */
    co_return co_await inner_->handle(path, std::move(req), std::move(rep));
}

/**
 * Strip les headers X-User-* venant du client.
 *
 * Implémentation case-insensitive : le client peut envoyer
 * "x-user-role" ou "X-USER-ROLE", on les attrape tous.
 */
void ProtectedHandler::strip_user_headers(seastar::http::request& req) const
{
    // On collecte les clés à supprimer (modification en cours d'itération
    // = comportement indéfini sur certaines maps).
    std::vector<seastar::sstring> to_remove;
    to_remove.reserve(8);

    for (const auto& kv : req._headers) {
        const auto& key = kv.first;

        // Compare case-insensitive avec "X-User-"
        if (key.size() < 7) {
            continue;
        }

        bool matches = true;
        static constexpr char prefix[] = "x-user-";
        for (std::size_t i = 0; i < 7; ++i) {
            const char c = static_cast<char>(
                std::tolower(static_cast<unsigned char>(key[i])));
            if (c != prefix[i]) {
                matches = false;
                break;
            }
        }

        if (matches) {
            to_remove.push_back(key);
        }
    }

    for (const auto& key : to_remove) {
        req._headers.erase(key);
    }
}

/**
 * Injecte les claims comme headers HTTP.
 *
 * Convention de nommage :
 *   user_id        → X-User-Id
 *   email          → X-User-Email
 *   role           → X-User-Role
 *   department_id  → X-User-Department-Id
 *   manager_id     → X-User-Manager-Id
 */
void ProtectedHandler::inject_claims_as_headers(
    seastar::http::request& req,
    const sea::application::AuthUserClaims& claims) const
{
    // Claims standards
    if (!claims.user_id.empty()) {
        req._headers["X-User-Id"] = claims.user_id;
    }
    if (!claims.email.empty()) {
        req._headers["X-User-Email"] = claims.email;
    }
    if (!claims.role.empty()) {
        req._headers["X-User-Role"] = claims.role;
    }

    // Claims custom (department_id, manager_id, etc.)
    for (const auto& [key, value] : claims.additional_claims) {
        if (key.empty() || value.empty()) {
            continue;
        }
        const std::string header_name = "X-User-" + to_header_case(key);
        req._headers[header_name] = value;
    }
}

/**
 * Convertit un nom de claim snake_case en Header-Case.
 *
 * Exemples :
 *   "department_id"  → "Department-Id"
 *   "mfa_verified"   → "Mfa-Verified"
 *   "tenant_id"      → "Tenant-Id"
 */
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


/**
 * Helper pour conditionnellement protéger une route
 */
std::unique_ptr<seastar::httpd::handler_base> maybe_protect(
    std::unique_ptr<seastar::httpd::handler_base> handler,
    bool requires_auth,
    const std::shared_ptr<sea::application::AuthService>& auth_service, const std::shared_ptr<IBlockingExecutor>& blocking_executor )
{
    if (requires_auth) {
        return std::make_unique<ProtectedHandler>(
            std::move(handler),
            auth_service,
            blocking_executor
            );
    }

    return handler;
}

} // namespace sea::http::handlers::auth