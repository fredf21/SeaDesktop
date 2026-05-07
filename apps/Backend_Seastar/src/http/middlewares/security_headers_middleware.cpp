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
    // POST-only middleware : on laisse passer la requete,
    // puis on enrichit la reponse avec les headers de securite
    auto reply = co_await inner_->handle(path, std::move(req), std::move(rep));

    if (config_.hsts()) {
        reply->_headers["Strict-Transport-Security"] = *config_.hsts();
    }
    if (config_.content_type_options()) {
        reply->_headers["X-Content-Type-Options"] = *config_.content_type_options();
    }
    if (config_.frame_options()) {
        reply->_headers["X-Frame-Options"] = *config_.frame_options();
    }
    if (config_.referrer_policy()) {
        reply->_headers["Referrer-Policy"] = *config_.referrer_policy();
    }

    // Approche fail-safe : on n'ecrase JAMAIS un CSP deja mis par le handler.
    // - Si le handler met son propre CSP (ex: /docs avec nonce), on respecte.
    // - Si le handler n'en met pas, on applique le CSP global comme garde-fou.
    // Cette approche evite tout trou de securite : aucune route ne peut se
    // retrouver sans CSP si un CSP global est configure.
    if (config_.content_security_policy()) {
        const bool handler_set_csp =
            reply->_headers.find("Content-Security-Policy") != reply->_headers.end();
        if (!handler_set_csp) {
            reply->_headers["Content-Security-Policy"] = *config_.content_security_policy();
        }
    }

    if (config_.permissions_policy()) {
        reply->_headers["Permissions-Policy"] = *config_.permissions_policy();
    }
    if (config_.cross_origin_opener_policy()) {
        reply->_headers["Cross-Origin-Opener-Policy"] = *config_.cross_origin_opener_policy();
    }
    if (config_.cross_origin_resource_policy()) {
        reply->_headers["Cross-Origin-Resource-Policy"] = *config_.cross_origin_resource_policy();
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