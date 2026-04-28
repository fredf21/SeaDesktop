#include "me_handler.h"
#include "../../utils/http_utils.h"

#include "runtime/generic_crud_engine.h"

#include <nlohmann/json.hpp>
#include <utility>

namespace sea::http::handlers::auth {

using json = nlohmann::json;

/**
 * MeHandler
 *
 * Route : GET /auth/me
 *
 * Rôle :
 * - récupérer l'utilisateur courant
 *
 * Important :
 * → suppose que le middleware (ProtectedHandler)
 *   a déjà validé le token et injecté les claims
 */
MeHandler::MeHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine)
    : crud_engine_(std::move(crud_engine))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
MeHandler::handle(const seastar::sstring&,
                  std::unique_ptr<seastar::http::request> req,
                  std::unique_ptr<seastar::http::reply> rep)
{
    /**
     * Récupération du user_id injecté par le middleware
     *
     * Exemple d'injection dans ProtectedHandler :
     * req->set_header("X-User-Id", claims->user_id);
     */
    const auto user_id = req->get_header("X-User-Id");

    if (user_id.empty()) {
        rep->set_status(seastar::http::reply::status_type::unauthorized);
        rep->write_body("application/json",
                        json{{"error", "Utilisateur non authentifie"}}.dump());
        co_return std::move(rep);
    }

    /**
     * Récupération utilisateur en base
     *
     * Si ton repo MySQL utilise le thread pool → pas de blocage ici
     */
    const auto user = co_await crud_engine_->get_by_id("User", user_id);

    if (!user.has_value()) {
        rep->set_status(seastar::http::reply::status_type::not_found);
        rep->write_body("application/json",
                        json{{"error", "Utilisateur introuvable"}}.dump());
        co_return std::move(rep);
    }

    /**
     * Sérialisation
     */
    json user_json =
        json::parse(sea::http::utils::record_to_json(*user));

    /**
     *  Sécurité : ne jamais exposer le password
     */
    user_json.erase("password");

    rep->set_status(seastar::http::reply::status_type::ok);
    rep->write_body("application/json", user_json.dump());

    co_return std::move(rep);
}

} // namespace sea::http::handlers::auth