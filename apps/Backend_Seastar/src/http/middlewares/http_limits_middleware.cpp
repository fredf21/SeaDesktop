#include "http_limits_middleware.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace sea::http::middlewares {
using json = nlohmann::json;

HttpLimitsMiddleware::HttpLimitsMiddleware(
    std::unique_ptr<seastar::httpd::handler_base> inner,
    sea::domain::security::HttpLimits config)
    : inner_(std::move(inner))
    , config_(std::move(config))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
HttpLimitsMiddleware::handle(
    const seastar::sstring& path,
    std::unique_ptr<seastar::http::request> req,
    std::unique_ptr<seastar::http::reply> rep)
{
    // 1. URL trop longue ?
    if (path.size() > config_.max_url_length()) {
        rep->set_status(seastar::http::reply::status_type::bad_request);
        rep->write_body("application/json", json{
                                                {"error", "uri_too_long"},
                                                {"message", "L'URL depasse la longueur maximale autorisee"},
                                                {"max_length", config_.max_url_length()}
                                            }.dump());
        co_return std::move(rep);
    }

    // 2. Body trop gros ?
    if (req->content.size() > config_.max_body_size()) {
        rep->set_status(seastar::http::reply::status_type::payload_too_large);
        rep->write_body("application/json", json{
                                                {"error", "payload_too_large"},
                                                {"message", "Le body depasse la taille maximale autorisee"},
                                                {"max_bytes", config_.max_body_size()}
                                            }.dump());
        co_return std::move(rep);
    }

    // 3. Trop de headers ?
    if (req->_headers.size() > config_.max_headers_count()) {
        rep->set_status(seastar::http::reply::status_type::bad_request);
        rep->write_body("application/json", json{
                                                {"error", "too_many_headers"},
                                                {"message", "Trop de headers dans la requete"},
                                                {"max_count", config_.max_headers_count()}
                                            }.dump());
        co_return std::move(rep);
    }

    // 4. Header trop long ?
    for (const auto& [name, value] : req->_headers) {
        if (value.size() > config_.max_header_size()) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("application/json", json{
                                                    {"error", "header_too_large"},
                                                    {"message", "Un header depasse la taille maximale"},
                                                    {"header_name", std::string(name)},
                                                    {"max_bytes", config_.max_header_size()}
                                                }.dump());
            co_return std::move(rep);
        }
    }

    // 5. Trop de query params ?
    if (req->query_parameters.size() > config_.max_query_params()) {
        rep->set_status(seastar::http::reply::status_type::bad_request);
        rep->write_body("application/json", json{
                                                {"error", "too_many_query_params"},
                                                {"message", "Trop de parametres dans l'URL"},
                                                {"max_count", config_.max_query_params()}
                                            }.dump());
        co_return std::move(rep);
    }

    // OK : on laisse passer au downstream
    co_return co_await inner_->handle(path, std::move(req), std::move(rep));
}

std::unique_ptr<seastar::httpd::handler_base> apply_http_limits(
    std::unique_ptr<seastar::httpd::handler_base> handler,
    sea::domain::security::HttpLimits config)
{
    return std::make_unique<HttpLimitsMiddleware>(
        std::move(handler),
        std::move(config)
        );
}

} // namespace sea::http::middlewares