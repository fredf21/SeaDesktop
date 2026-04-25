#include "list_by_fk_handler.h"
#include "../../utils/http_utils.h"

using namespace sea::http::handlers::relation;

ListByFkHandler::ListByFkHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::string child_entity,
    std::string fk_column)
    : crud_engine_(std::move(crud_engine))
    , child_entity_(std::move(child_entity))
    , fk_column_(std::move(fk_column))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
ListByFkHandler::handle(const seastar::sstring&,
                        std::unique_ptr<seastar::http::request> req,
                        std::unique_ptr<seastar::http::reply> rep)
{
    const auto parent_id = req->get_path_param("id");
    if (parent_id.empty()) {
        rep->set_status(seastar::http::reply::status_type::bad_request);
        rep->write_body("application/json", R"({"error":"id manquant"})");
        co_return std::move(rep);
    }

    const auto records = co_await crud_engine_->list(child_entity_);

    std::vector<infrastructure::runtime::DynamicRecord> filtered;

    for (const auto& r : records) {
        auto it = r.find(fk_column_);
        if (it == r.end()) continue;

        auto id = utils::dynamic_value_to_string_id(it->second);
        if (id.has_value() && *id == parent_id) {
            filtered.push_back(r);
        }
    }

    rep->set_status(seastar::http::reply::status_type::ok);
    rep->write_body("application/json", utils::records_to_json(filtered));
    co_return std::move(rep);
}
