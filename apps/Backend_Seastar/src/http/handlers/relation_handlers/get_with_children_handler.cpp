#include "get_with_children_handler.h"
#include "../access_control/resource_authorization_helper.h"
#include "../../utils/http_utils.h"

#include "access_control/crud_operation.h"

#include <nlohmann/json.hpp>
#include <utility>

using namespace sea::http::handlers::relation;

GetWithChildrenHandler::GetWithChildrenHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::string parent_entity,
    std::string child_entity,
    std::string fk_column,
    std::string children_key,
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper)
    : crud_engine_(std::move(crud_engine))
    , parent_entity_(std::move(parent_entity))
    , child_entity_(std::move(child_entity))
    , fk_column_(std::move(fk_column))
    , children_key_(std::move(children_key))
    , auth_helper_(std::move(auth_helper))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
GetWithChildrenHandler::handle(const seastar::sstring&,
                               std::unique_ptr<seastar::http::request> req,
                               std::unique_ptr<seastar::http::reply> rep)
{
    const auto id = req->get_path_param("id");

    auto parent = co_await crud_engine_->get_by_id(parent_entity_, std::string(id));
    if (!parent.has_value()) {
        rep->set_status(seastar::http::reply::status_type::not_found);
        rep->write_body("application/json", R"({"error":"parent introuvable"})");
        co_return std::move(rep);
    }

    const std::string parent_json = utils::record_to_json(*parent);

    // check ABAC sur le PARENT (single)
    if (auth_helper_) {
        const auto subject = auth_helper_->build_subject_from_headers(*req);

        const std::string path_str(req->_url.data(), req->_url.size());
        const auto context = auth_helper_->build_context(*req, path_str);

        const auto check = auth_helper_->check_single(
            parent_entity_,
            sea::domain::access_control::CrudOperation::GetById,
            subject,
            parent_json,
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

    // Charge les enfants
    const auto children = co_await crud_engine_->list(child_entity_);

    std::vector<infrastructure::runtime::DynamicRecord> filtered;

    for (const auto& c : children) {
        auto it = c.find(fk_column_);
        if (it == c.end()) continue;

        auto fk = utils::dynamic_value_to_string_id(it->second);
        if (fk.has_value() && *fk == id) {
            filtered.push_back(c);
        }
    }

    std::string children_json = utils::records_to_json(filtered);

    // filter ABAC sur les ENFANTS (collection)
    if (auth_helper_) {
        const auto subject = auth_helper_->build_subject_from_headers(*req);

        const std::string path_str(req->_url.data(), req->_url.size());
        const auto context = auth_helper_->build_context(*req, path_str);

        children_json = auth_helper_->filter_collection(
            child_entity_,
            sea::domain::access_control::CrudOperation::List,
            subject,
            children_json,
            context
            );
    }

    // Construit la reponse finale : parent + children_filtre
    std::string result = parent_json;
    result.pop_back(); // enleve le }
    result += ", \"" + children_key_ + "\": ";
    result += children_json;
    result += "}";

    rep->set_status(seastar::http::reply::status_type::ok);
    rep->write_body("application/json", result);

    co_return std::move(rep);
}