#include "list_by_fk_field_handler.h"
#include "../access_control/resource_authorization_helper.h"
#include "../../utils/http_utils.h"

#include "access_control/crud_operation.h"

#include <utility>

using namespace sea::http::handlers::relation;

ListByFkFieldHandler::ListByFkFieldHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::string child_entity,
    std::string parent_entity,
    std::string fk_column,
    std::string search_field,
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper)
    : crud_engine_(std::move(crud_engine))
    , child_entity_(std::move(child_entity))
    , parent_entity_(std::move(parent_entity))
    , fk_column_(std::move(fk_column))
    , search_field_(std::move(search_field))
    , auth_helper_(std::move(auth_helper))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
ListByFkFieldHandler::handle(const seastar::sstring&,
                             std::unique_ptr<seastar::http::request> req,
                             std::unique_ptr<seastar::http::reply> rep)
{
    // ✨ Path param "value" au lieu de query param
    // Route : /<children>/filter/with_<parent>_<field>/{value}
    // Ex: /employees/filter/with_department_name/IT
    const auto value = req->get_path_param("value");
    if (value.empty()) {
        rep->set_status(seastar::http::reply::status_type::bad_request);
        rep->write_body("application/json", R"({"error":"value manquant"})");
        co_return std::move(rep);
    }

    const auto parents = co_await crud_engine_->list(parent_entity_);

    std::optional<std::string> parent_id;

    for (const auto& p : parents) {
        const auto field_it = p.find(search_field_);
        if (field_it == p.end()) {
            continue;
        }

        if (!sea::http::utils::dynamic_value_matches_string(field_it->second, std::string(value))) {
            continue;
        }

        const auto id_it = p.find("id");
        if (id_it == p.end()) {
            continue;
        }

        parent_id = sea::http::utils::dynamic_value_to_string_id(id_it->second);
        break;
    }

    if (!parent_id.has_value()) {
        rep->set_status(seastar::http::reply::status_type::not_found);
        rep->write_body("application/json", R"({"error":"parent introuvable"})");
        co_return std::move(rep);
    }

    const auto children = co_await crud_engine_->list(child_entity_);

    std::vector<infrastructure::runtime::DynamicRecord> result;

    for (const auto& c : children) {
        auto it = c.find(fk_column_);
        if (it == c.end()) continue;

        auto id = utils::dynamic_value_to_string_id(it->second);
        if (id.has_value() && *id == *parent_id) {
            result.push_back(c);
        }
    }

    std::string final_json = utils::records_to_json(result);

    // filter ABAC silencieux sur la collection
    if (auth_helper_) {
        const auto subject = auth_helper_->build_subject_from_headers(*req);

        const std::string path_str(req->_url.data(), req->_url.size());
        const auto context = auth_helper_->build_context(*req, path_str);

        final_json = auth_helper_->filter_collection(
            child_entity_,
            sea::domain::access_control::CrudOperation::List,
            subject,
            final_json,
            context
            );
    }

    rep->set_status(seastar::http::reply::status_type::ok);
    rep->write_body("application/json", final_json);
    co_return std::move(rep);
}