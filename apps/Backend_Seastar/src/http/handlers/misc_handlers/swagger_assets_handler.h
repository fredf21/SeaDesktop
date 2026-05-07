#pragma once
#include <seastar/http/httpd.hh>

namespace sea::http::handlers::misc {

/**
 * Handler pour servir les fichiers JS/CSS/PNG embeddes
 * de Swagger UI.
 *
 * Routes gerees :
 *   GET /assets/swagger-ui/swagger-ui.css
 *   GET /assets/swagger-ui/swagger-ui-bundle.js
 *   GET /assets/swagger-ui/swagger-ui-standalone-preset.js
 *   GET /assets/swagger-ui/favicon-32x32.png
 *
 * Tous les fichiers sont en memoire (zero I/O disque).
 * Cache HTTP : Cache-Control: public, max-age=31536000, immutable
 * (les versions sont fixees dans le binaire).
 */
class SwaggerAssetsHandler final : public seastar::httpd::handler_base {
public:
    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring& path,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override;
};

} // namespace sea::http::handlers::misc