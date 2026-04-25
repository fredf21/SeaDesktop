#pragma once
#include <seastar/http/httpd.hh>

namespace sea::http::handlers::misc {

class SwaggerUiHandler final : public seastar::httpd::handler_base {
public:
    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request>,
           std::unique_ptr<seastar::http::reply> rep) override;
};

}
