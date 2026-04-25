#include "list_handler.h"
#include "../../utils/http_utils.h"

#include "runtime/generic_crud_engine.h"

#include <utility>

namespace sea::http::handlers::crud {

ListHandler::ListHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::string entity_name)
    : crud_engine_(std::move(crud_engine))
    , entity_name_(std::move(entity_name))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
ListHandler::handle(const seastar::sstring&,
                    std::unique_ptr<seastar::http::request>,
                    std::unique_ptr<seastar::http::reply> rep)
{
    const auto records = co_await crud_engine_->list(entity_name_);
    rep->set_status(seastar::http::reply::status_type::ok);
    rep->write_body("application/json", sea::http::utils::records_to_json(records));
    co_return std::move(rep);
}

} // namespace sea::http::handlers::crud
