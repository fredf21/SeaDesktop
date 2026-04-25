#include "openapi_handler.h"

#include <utility>

namespace sea::http::handlers::misc {

OpenApiHandler::OpenApiHandler(std::string body)
    : body_(std::move(body))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
OpenApiHandler::handle(const seastar::sstring&,
                       std::unique_ptr<seastar::http::request>,
                       std::unique_ptr<seastar::http::reply> rep)
{
    rep->set_status(seastar::http::reply::status_type::ok);
    rep->write_body("application/json", body_);
    co_return std::move(rep);
}

} // namespace sea::http::handlers::misc
