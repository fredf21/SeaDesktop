#include "list_by_fk_field_handler.h"
#include "../../utils/http_utils.h"

using namespace sea::http::handlers::relation;

ListByFkFieldHandler::ListByFkFieldHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::string child_entity,
    std::string parent_entity,
    std::string fk_column,
    std::string search_field)
    : crud_engine_(std::move(crud_engine))
    , child_entity_(std::move(child_entity))
    , parent_entity_(std::move(parent_entity))
    , fk_column_(std::move(fk_column))
    , search_field_(std::move(search_field))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
ListByFkFieldHandler::handle(const seastar::sstring&,
                             std::unique_ptr<seastar::http::request> req,
                             std::unique_ptr<seastar::http::reply> rep)
{
    const auto value = req->get_query_param(search_field_);
    if (value.empty()) {
        rep->set_status(seastar::http::reply::status_type::bad_request);
        rep->write_body("application/json", R"({"error":"param manquant"})");
        co_return std::move(rep);
    }

    const auto parents = co_await crud_engine_->list(parent_entity_);

    std::optional<std::string> parent_id;

    for (const auto& p : parents) {
        const auto field_it = p.find(search_field_);
        if (field_it == p.end()) {
            continue;
        }

        if (!sea::http::utils::dynamic_value_matches_string(field_it->second, value)) {
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

    rep->set_status(seastar::http::reply::status_type::ok);
    rep->write_body("application/json", utils::records_to_json(result));
    co_return std::move(rep);
}
