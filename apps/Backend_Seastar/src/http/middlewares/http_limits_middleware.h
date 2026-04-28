#ifndef HTTP_LIMITS_MIDDLEWARE_H
#define HTTP_LIMITS_MIDDLEWARE_H

#include "security_scheme/http_limit.h"

#include <seastar/http/handlers.hh>
#include <seastar/http/reply.hh>
#include <seastar/http/request.hh>

#include <memory>

namespace sea::http::middlewares {

class HttpLimitsMiddleware : public seastar::httpd::handler_base {
public:
    HttpLimitsMiddleware(
        std::unique_ptr<seastar::httpd::handler_base> inner,
        sea::domain::security::HttpLimits config
        );

    seastar::future<std::unique_ptr<seastar::http::reply>> handle(
        const seastar::sstring& path,
        std::unique_ptr<seastar::http::request> req,
        std::unique_ptr<seastar::http::reply> rep
        ) override;

private:
    std::unique_ptr<seastar::httpd::handler_base> inner_;
    sea::domain::security::HttpLimits config_;
};

// Helper de chaînage
std::unique_ptr<seastar::httpd::handler_base> apply_http_limits(
    std::unique_ptr<seastar::httpd::handler_base> handler,
    sea::domain::security::HttpLimits config
    );

} // namespace sea::http::middlewares

#endif // HTTP_LIMITS_MIDDLEWARE_H