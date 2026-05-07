#include "get_one_by_fk_handler.h"
#include "../access_control/resource_authorization_helper.h"
#include "../../utils/http_utils.h"

#include "access_control/crud_operation.h"

#include <nlohmann/json.hpp>
#include <utility>

using namespace sea::http::handlers::relation;

GetOneByFkHandler::GetOneByFkHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::string child_entity,
    std::string fk_column,
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper)
    : crud_engine_(std::move(crud_engine))
    , child_entity_(std::move(child_entity))
    , fk_column_(std::move(fk_column))
    , auth_helper_(std::move(auth_helper))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
GetOneByFkHandler::handle(const seastar::sstring&,
                          std::unique_ptr<seastar::http::request> req,
                          std::unique_ptr<seastar::http::reply> rep)
{
    const auto parent_id = req->get_path_param("id");

    const auto children = co_await crud_engine_->list(child_entity_);

    for (const auto& c : children) {
        auto it = c.find(fk_column_);
        if (it == c.end()) continue;

        auto id = utils::dynamic_value_to_string_id(it->second);
        if (id.has_value() && *id == parent_id) {
            const std::string record_json = utils::record_to_json(c);

            // check ABAC resource-aware (comme GetById)
            if (auth_helper_) {
                const auto subject = auth_helper_->build_subject_from_headers(*req);

                const std::string path_str(req->_url.data(), req->_url.size());
                const auto context = auth_helper_->build_context(*req, path_str);

                const auto check = auth_helper_->check_single(
                    child_entity_,
                    sea::domain::access_control::CrudOperation::GetById,
                    subject,
                    record_json,
                    context
                    );

                if (!check.allowed) {
                    rep->set_status(seastar::http::reply::status_type::forbidden);
                    rep->write_body("application/json",
                                    nlohmann::json{
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
    }

    rep->set_status(seastar::http::reply::status_type::not_found);
    rep->write_body("application/json", R"({"error":"introuvable"})");
    co_return std::move(rep);
}