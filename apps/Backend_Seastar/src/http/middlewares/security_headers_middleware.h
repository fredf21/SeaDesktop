#ifndef SECURITY_HEADERS_MIDDLEWARE_H
#define SECURITY_HEADERS_MIDDLEWARE_H

#include "security_scheme/security_headers.h"

#include <seastar/http/handlers.hh>
#include <seastar/http/reply.hh>
#include <seastar/http/request.hh>

#include <memory>

namespace sea::http::middlewares {

class SecurityHeadersMiddleware : public seastar::httpd::handler_base {
public:
    SecurityHeadersMiddleware(
        std::unique_ptr<seastar::httpd::handler_base> inner,
        sea::domain::security::SecurityHeaders config
    );

    seastar::future<std::unique_ptr<seastar::http::reply>> handle(
        const seastar::sstring& path,
        std::unique_ptr<seastar::http::request> req,
        std::unique_ptr<seastar::http::reply> rep
    ) override;

private:
    std::unique_ptr<seastar::httpd::handler_base> inner_;
    sea::domain::security::SecurityHeaders config_;
};

// Helper de chaînage : retourne unique_ptr pour pouvoir wrapper d'autres middlewares
std::unique_ptr<seastar::httpd::handler_base> apply_security_headers(
    std::unique_ptr<seastar::httpd::handler_base> handler,
    sea::domain::security::SecurityHeaders config
);

} // namespace sea::http::middlewares

#endif // SECURITY_HEADERS_MIDDLEWARE_H
