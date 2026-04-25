#include "delete_handler.h"

#include "runtime/generic_crud_engine.h"

#include <nlohmann/json.hpp>
#include <utility>

namespace sea::http::handlers::crud {

using json = nlohmann::json;

DeleteHandler::DeleteHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::string entity_name)
    : crud_engine_(std::move(crud_engine))
    , entity_name_(std::move(entity_name))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
DeleteHandler::handle(const seastar::sstring&,
                      std::unique_ptr<seastar::http::request> req,
                      std::unique_ptr<seastar::http::reply> rep)
{
    const auto id = req->get_path_param("id");
    if (id.empty()) {
        rep->set_status(seastar::http::reply::status_type::bad_request);
        rep->write_body("application/json", json{{"error", "Parametre 'id' manquant."}}.dump());
        co_return std::move(rep);
    }

    try {
        const bool deleted = co_await crud_engine_->remove(entity_name_, std::string(id));
        if (!deleted) {
            rep->set_status(seastar::http::reply::status_type::not_found);
            rep->write_body("application/json", json{{"error", "Enregistrement introuvable."}}.dump());
            co_return std::move(rep);
        }

        rep->set_status(seastar::http::reply::status_type::ok);
        rep->write_body("application/json", json{{"message", "Deleted"}}.dump());
        co_return std::move(rep);

    } catch (const std::exception& e) {
        rep->set_status(seastar::http::reply::status_type::internal_server_error);
        rep->write_body("application/json", json{{"error", e.what()}}.dump());
        co_return std::move(rep);
    }
}

} // namespace sea::http::handlers::crud
