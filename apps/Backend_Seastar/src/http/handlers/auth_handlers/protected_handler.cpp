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
     * Passage au handler réel
     */
    co_return co_await inner_->handle(path, std::move(req), std::move(rep));
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