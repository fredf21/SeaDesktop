#include "get_by_id_handler.h"
#include "../access_control/resource_authorization_helper.h"
#include "../../utils/http_utils.h"

#include "access_control/crud_operation.h"
#include "runtime/generic_crud_engine.h"

#include <nlohmann/json.hpp>
#include <utility>

namespace sea::http::handlers::crud {

using json = nlohmann::json;

GetByIdHandler::GetByIdHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::string entity_name,
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper)
    : crud_engine_(std::move(crud_engine))
    , entity_name_(std::move(entity_name))
    , auth_helper_(std::move(auth_helper))
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

    const std::string record_json = sea::http::utils::record_to_json(*record);

    // check ABAC resource-aware
    // - Si auth_helper est null → bypass (autorisation desactivee)
    // - Sinon → evalue la regle complete (subject + resource)
    if (auth_helper_) {
        const auto subject = auth_helper_->build_subject_from_headers(*req);

        const std::string path_str(req->_url.data(), req->_url.size());
        const auto context = auth_helper_->build_context(*req, path_str);

        const auto check = auth_helper_->check_single(
            entity_name_,
            sea::domain::access_control::CrudOperation::GetById,
            subject,
            record_json,
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

    rep->set_status(seastar::http::reply::status_type::ok);
    rep->write_body("application/json", record_json);
    co_return std::move(rep);
}

} // namespace sea::http::handlers::crud