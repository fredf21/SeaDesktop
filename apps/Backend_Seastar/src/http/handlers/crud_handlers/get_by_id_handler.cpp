#include "get_by_id_handler.h"
#include "../../utils/http_utils.h"

#include "runtime/generic_crud_engine.h"

#include <nlohmann/json.hpp>
#include <utility>

namespace sea::http::handlers::crud {

using json = nlohmann::json;

GetByIdHandler::GetByIdHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::string entity_name)
    : crud_engine_(std::move(crud_engine))
    , entity_name_(std::move(entity_name))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
GetByIdHandler::handle(const seastar::sstring&,
                       std::unique_ptr<seastar::http::request> req,
                       std::unique_ptr<seastar::http::reply> rep)
{
    const auto id = req->get_path_param("id");
    if (id.empty()) {
        rep->set_status(seastar::http::reply::status_type::bad_request);
        rep->write_body("application/json", json{{"error", "Parametre 'id' manquant."}}.dump());
        co_return std::move(rep);
    }

    const auto record = co_await crud_engine_->get_by_id(entity_name_, std::string(id));
    if (!record.has_value()) {
        rep->set_status(seastar::http::reply::status_type::not_found);
        rep->write_body("application/json", json{{"error", "Enregistrement introuvable."}}.dump());
        co_return std::move(rep);
    }

    rep->set_status(seastar::http::reply::status_type::ok);
    rep->write_body("application/json", sea::http::utils::record_to_json(*record));
    co_return std::move(rep);
}

} // namespace sea::http::handlers::crud
