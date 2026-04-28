#include "cors_middleware.h"

#include "protocol/http_protocol/http_method.h"

#include <utility>

namespace sea::http::middlewares {

CorsMiddleware::CorsMiddleware(
    std::unique_ptr<seastar::httpd::handler_base> inner,
    sea::domain::security::CorsConfig config)
    : inner_(std::move(inner))
    , config_(std::move(config))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
CorsMiddleware::handle(
    const seastar::sstring& path,
    std::unique_ptr<seastar::http::request> req,
    std::unique_ptr<seastar::http::reply> rep)
{
    const std::string origin = get_origin(*req);

    // 1. Preflight OPTIONS : on gère directement, sans appeler le downstream
    if (req->_method == "OPTIONS") {
        co_return co_await handle_preflight(origin, std::move(rep));
    }

    // 2. Si une origin est fournie mais pas autorisée → 403
    if (!origin.empty() && !config_.allows_origin(origin)) {
        rep->set_status(seastar::http::reply::status_type::forbidden);
        rep->write_body("application/json",
                        R"({"error":"cors_forbidden","message":"Origin not allowed"})");
        co_return std::move(rep);
    }

    // 3. On laisse passer au downstream + on enrichit la réponse
    auto reply = co_await inner_->handle(path, std::move(req), std::move(rep));
    add_cors_headers(*reply, origin);
    co_return std::move(reply);
}

seastar::future<std::unique_ptr<seastar::http::reply>>
CorsMiddleware::handle_preflight(
    const std::string& origin,
    std::unique_ptr<seastar::http::reply> rep)
{
    // Pas d'origin ou origin non autorisée → 403
    if (origin.empty() || !config_.allows_origin(origin)) {
        rep->set_status(seastar::http::reply::status_type::forbidden);
        co_return std::move(rep);
    }

    // Origin OK : on répond aux headers du preflight
    rep->add_header("Access-Control-Allow-Origin", origin);

    if (!config_.allowed_methods().empty()) {
        rep->add_header("Access-Control-Allow-Methods",
                        join_methods(config_.allowed_methods()));
    }

    if (!config_.allowed_headers().empty()) {
        rep->add_header("Access-Control-Allow-Headers",
                        join_strings(config_.allowed_headers()));
    }

    if (config_.allow_credentials()) {
        rep->add_header("Access-Control-Allow-Credentials", "true");
    }

    if (config_.max_age().count() > 0) {
        rep->add_header("Access-Control-Max-Age",
                        std::to_string(config_.max_age().count()));
    }

    rep->set_status(seastar::http::reply::status_type::no_content);
    co_return std::move(rep);
}

void CorsMiddleware::add_cors_headers(
    seastar::http::reply& rep,
    const std::string& origin) const
{
    // Pas d'origin ou origin non autorisée : on n'ajoute pas de headers
    if (origin.empty() || !config_.allows_origin(origin)) {
        return;
    }

    rep.add_header("Access-Control-Allow-Origin", origin);

    if (config_.allow_credentials()) {
        rep.add_header("Access-Control-Allow-Credentials", "true");
    }

    if (!config_.exposed_headers().empty()) {
        rep.add_header("Access-Control-Expose-Headers",
                       join_strings(config_.exposed_headers()));
    }

    // Vary: Origin pour que les caches (CDN, navigateur) sachent que la
    // réponse dépend de l'origin
    rep.add_header("Vary", "Origin");
}

std::string CorsMiddleware::get_origin(const seastar::http::request& req) const
{
    auto it = req._headers.find("Origin");
    if (it == req._headers.end()) {
        return "";
    }
    return std::string(it->second);
}

std::string CorsMiddleware::join_strings(const std::vector<std::string>& items)
{
    std::string result;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) result += ", ";
        result += items[i];
    }
    return result;
}

std::string CorsMiddleware::join_methods(
    const std::vector<sea::domain::security::HttpMethod>& methods)
{
    std::string result;
    for (std::size_t i = 0; i < methods.size(); ++i) {
        if (i > 0) result += ", ";
        result += std::string(sea::domain::http::to_string(methods[i]));
    }
    return result;
}

std::unique_ptr<seastar::httpd::handler_base> apply_cors(
    std::unique_ptr<seastar::httpd::handler_base> handler,
    sea::domain::security::CorsConfig config)
{
    return std::make_unique<CorsMiddleware>(
        std::move(handler),
        std::move(config)
        );
}

} // namespace sea::http::middlewares