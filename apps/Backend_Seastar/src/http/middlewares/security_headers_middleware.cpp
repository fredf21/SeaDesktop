#include "security_headers_middleware.h"

#include <utility>

namespace sea::http::middlewares {

SecurityHeadersMiddleware::SecurityHeadersMiddleware(
    std::unique_ptr<seastar::httpd::handler_base> inner,
    sea::domain::security::SecurityHeaders config)
    : inner_(std::move(inner))
    , config_(std::move(config))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
SecurityHeadersMiddleware::handle(
    const seastar::sstring& path,
    std::unique_ptr<seastar::http::request> req,
    std::unique_ptr<seastar::http::reply> rep)
{
    // POST-only middleware : on laisse passer la requête,
    // puis on enrichit la réponse avec les headers de sécurité
    auto reply = co_await inner_->handle(path, std::move(req), std::move(rep));

    if (config_.hsts()) {
        reply->add_header("Strict-Transport-Security", *config_.hsts());
    }
    if (config_.content_type_options()) {
        reply->add_header("X-Content-Type-Options", *config_.content_type_options());
    }
    if (config_.frame_options()) {
        reply->add_header("X-Frame-Options", *config_.frame_options());
    }
    if (config_.referrer_policy()) {
        reply->add_header("Referrer-Policy", *config_.referrer_policy());
    }
    if (config_.content_security_policy()) {
        reply->add_header("Content-Security-Policy", *config_.content_security_policy());
    }
    if (config_.permissions_policy()) {
        reply->add_header("Permissions-Policy", *config_.permissions_policy());
    }
    if (config_.cross_origin_opener_policy()) {
        reply->add_header("Cross-Origin-Opener-Policy", *config_.cross_origin_opener_policy());
    }
    if (config_.cross_origin_resource_policy()) {
        reply->add_header("Cross-Origin-Resource-Policy", *config_.cross_origin_resource_policy());
    }

    co_return std::move(reply);
}

std::unique_ptr<seastar::httpd::handler_base> apply_security_headers(
    std::unique_ptr<seastar::httpd::handler_base> handler,
    sea::domain::security::SecurityHeaders config)
{
    return std::make_unique<SecurityHeadersMiddleware>(
        std::move(handler),
        std::move(config)
    );
}

} // namespace sea::http::middlewares
