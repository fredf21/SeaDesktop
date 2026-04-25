#include "get_with_children_handler.h"
#include "../../utils/http_utils.h"

using namespace sea::http::handlers::relation;

GetWithChildrenHandler::GetWithChildrenHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::string parent_entity,
    std::string child_entity,
    std::string fk_column,
    std::string children_key)
    : crud_engine_(std::move(crud_engine))
    , parent_entity_(std::move(parent_entity))
    , child_entity_(std::move(child_entity))
    , fk_column_(std::move(fk_column))
    , children_key_(std::move(children_key))
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

    std::string json = utils::record_to_json(*parent);
    json.pop_back(); // enlever }

    json += ", \"" + children_key_ + "\": ";
    json += utils::records_to_json(filtered);
    json += "}";

    rep->set_status(seastar::http::reply::status_type::ok);
    rep->write_body("application/json", json);

    co_return std::move(rep);
}
