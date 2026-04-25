#include "me_handler.h"
#include "../../utils/http_utils.h"

#include "authservice.h"
#include "runtime/generic_crud_engine.h"

#include <nlohmann/json.hpp>
#include <utility>

namespace sea::http::handlers::auth {

using json = nlohmann::json;

MeHandler::MeHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::shared_ptr<sea::application::AuthService> auth_service)
    : crud_engine_(std::move(crud_engine))
    , auth_service_(std::move(auth_service))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
MeHandler::handle(const seastar::sstring&,
                  std::unique_ptr<seastar::http::request> req,
                  std::unique_ptr<seastar::http::reply> rep)
{
    const auto token = sea::http::utils::extract_bearer_token(*req);

    if (!token.has_value()) {
        rep->set_status(seastar::http::reply::status_type::unauthorized);
        rep->write_body("application/json", R"({"error":"Token manquant"})");
        co_return std::move(rep);
    }

    const auto claims = auth_service_->verify_token(*token);

    if (!claims.has_value()) {
        rep->set_status(seastar::http::reply::status_type::unauthorized);
        rep->write_body("application/json", R"({"error":"Token invalide"})");
        co_return std::move(rep);
    }

    const auto user = co_await crud_engine_->get_by_id("User", claims->user_id);

    if (!user.has_value()) {
        rep->set_status(seastar::http::reply::status_type::not_found);
        rep->write_body("application/json", R"({"error":"Utilisateur introuvable"})");
        co_return std::move(rep);
    }

    const auto json = sea::http::utils::record_to_json(*user);

    rep->set_status(seastar::http::reply::status_type::ok);
    rep->write_body("application/json", json);

    co_return std::move(rep);
}

} // namespace sea::http::handlers::auth
