#include "delete_handler.h"
#include "../access_control/resource_authorization_helper.h"
#include "../../utils/http_utils.h"

#include "access_control/crud_operation.h"
#include "runtime/generic_crud_engine.h"

#include <nlohmann/json.hpp>
#include <utility>

namespace sea::http::handlers::crud {

using json = nlohmann::json;

DeleteHandler::DeleteHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::string entity_name,
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper)
    : crud_engine_(std::move(crud_engine))
    , entity_name_(std::move(entity_name))
    , auth_helper_(std::move(auth_helper))
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
        // check ABAC AVANT suppression
        // On charge la ressource pour evaluer les regles (own_resource, same_scope, etc.)
        if (auth_helper_) {
            const auto current = co_await crud_engine_->get_by_id(
                entity_name_, std::string(id)
                );

            if (!current.has_value()) {
                rep->set_status(seastar::http::reply::status_type::not_found);
                rep->write_body("application/json",
                                json{{"error", "Enregistrement introuvable."}}.dump());
                co_return std::move(rep);
            }

            const std::string current_json = sea::http::utils::record_to_json(*current);

            const auto subject = auth_helper_->build_subject_from_headers(*req);

            const std::string path_str(req->_url.data(), req->_url.size());
            const auto context = auth_helper_->build_context(*req, path_str);

            const auto check = auth_helper_->check_single(
                entity_name_,
                sea::domain::access_control::CrudOperation::Delete,
                subject,
                current_json,
                context
                );

            if (!check.allowed) {
                rep->set_status(seastar::http::reply::status_type::forbidden);
                rep->write_body("application/json",
                                json{
                                    {"error", "Forbidden"},
                                    {"message", check.reason}
                                }.dump());
                co_return std::move(rep);
            }
        }

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