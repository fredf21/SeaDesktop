#pragma once
#include <seastar/http/httpd.hh>
#include <string>

namespace sea::http::handlers::misc {

class OpenApiHandler final : public seastar::httpd::handler_base {
public:
    explicit OpenApiHandler(std::string body);

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request>,
           std::unique_ptr<seastar::http::reply> rep) override;

private:
    std::string body_;
};

}
