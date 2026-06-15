#include "restart_handler.h"

#include "../../errors/error_response_factory.h"

#include <nlohmann/json.hpp>
#include <seastar/core/sleep.hh>
#include <seastar/core/future.hh>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdlib>
#include <utility>

namespace sea::http::handlers::admin {

using json = nlohmann::json;
namespace errors = sea::http::errors;
using Status = seastar::http::reply::status_type;

namespace {

/**
 * Verifie que le user a le role admin via le header X-User-Role
 * injecte par ProtectedHandler. Retourne nullptr si OK, sinon une
 * reply pre-remplie (401/403) que l'appelant doit retourner.
 */
[[nodiscard]] std::unique_ptr<seastar::http::reply>
check_admin_role(const seastar::http::request& req,
                 const std::string& expected_admin_role)
{
    const auto role_it = req._headers.find("X-User-Role");
    if (role_it == req._headers.end() || role_it->second.empty()) {
        return errors::make_error_reply(
            Status::unauthorized, "AUTHENTICATION_ERROR",
            "Authentication required.");
    }

    const std::string_view role_view(
        role_it->second.data(), role_it->second.size()
        );
    if (role_view != expected_admin_role) {
        return errors::make_error_reply(
            Status::forbidden, "AUTHORIZATION_ERROR",
            "Admin role required.");
    }
    return nullptr;
}

} // namespace anonyme


RestartHandler::RestartHandler(std::string admin_role)
    : admin_role_(std::move(admin_role))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
RestartHandler::handle(const seastar::sstring&,
                       std::unique_ptr<seastar::http::request> req,
                       std::unique_ptr<seastar::http::reply> rep)
{
    // ─── 1. Garde role admin ─────────────────────────────────────
    if (auto guard = check_admin_role(*req, admin_role_); guard) {
        co_return std::move(guard);
    }

    // ─── 2. Planifie l'arret du process ──────────────────────────
    // On lance une coroutine en arriere-plan qui attend 500ms puis
    // termine le process. Le delai donne le temps au reactor de :
    //   - finir d'ecrire la reponse HTTP sur le socket
    //   - fermer proprement la connexion TCP
    //   - retourner du handler (la coroutine principale)
    //
    // Note : (void)future signifie qu'on ne wait PAS dessus dans le
    // handler. Le handler retourne sa reponse immediatement, et
    // l'arret du process se produit en arriere-plan.
    if (auto log = spdlog::get("sea.boot")) {
        log->info(
            "RestartHandler: restart requested, process will exit "
            "in 500ms (Docker should restart the container).");
    }

    (void) seastar::sleep(std::chrono::milliseconds(500))
        .then([] {
            if (auto log = spdlog::get("sea.boot")) {
                log->info("RestartHandler: exiting now.");
            }
            std::_Exit(0);
        });

    // ─── 3. Reponse 202 Accepted ─────────────────────────────────
    // 202 plutot que 200 car le travail demande (restart) n'est pas
    // termine au moment ou on envoie la reponse.
    json response;
    response["success"] = true;
    response["message"] = "Service restarting";

    rep->set_status(Status::accepted);
    rep->write_body("application/json", response.dump());

    co_return std::move(rep);
}

} // namespace sea::http::handlers::admin