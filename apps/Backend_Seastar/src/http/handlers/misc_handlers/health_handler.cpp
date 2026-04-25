#include "health_handler.h"

namespace sea::http::handlers::misc {

seastar::future<std::unique_ptr<seastar::http::reply>>
HealthHandler::handle(const seastar::sstring&,
                      std::unique_ptr<seastar::http::request>,
                      std::unique_ptr<seastar::http::reply> rep)
{
    static const seastar::sstring body = R"({"status":"RUNNING"})";
    rep->set_status(seastar::http::reply::status_type::ok);
    rep->write_body("application/json", body);
    return seastar::make_ready_future<std::unique_ptr<seastar::http::reply>>(
        std::move(rep)
        );}

} // namespace sea::http::handlers::misc
