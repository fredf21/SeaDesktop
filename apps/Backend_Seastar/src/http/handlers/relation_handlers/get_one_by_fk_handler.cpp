#include "get_one_by_fk_handler.h"
#include "../../utils/http_utils.h"

using namespace sea::http::handlers::relation;

GetOneByFkHandler::GetOneByFkHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::string child_entity,
    std::string fk_column)
    : crud_engine_(std::move(crud_engine))
    , child_entity_(std::move(child_entity))
    , fk_column_(std::move(fk_column))
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
            rep->set_status(seastar::http::reply::status_type::ok);
            rep->write_body("application/json", utils::record_to_json(c));
            co_return std::move(rep);
        }
    }

    rep->set_status(seastar::http::reply::status_type::not_found);
    rep->write_body("application/json", R"({"error":"introuvable"})");
    co_return std::move(rep);
}
