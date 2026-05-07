#include "swagger_ui_handler.h"
#include "http/utils/nonce_generator.h"

#include <sstream>

namespace sea::http::handlers::misc {

seastar::future<std::unique_ptr<seastar::http::reply>>
SwaggerUiHandler::handle(const seastar::sstring&,
                         std::unique_ptr<seastar::http::request>,
                         std::unique_ptr<seastar::http::reply> rep)
{
    // ─── Genere un nonce unique pour cette requete ───
    const auto nonce = sea::http::utils::generate_csp_nonce();

    // ─── HTML avec nonce injecte dans <style> et <script> inline ───
    std::ostringstream html;
    html << R"HTML(<!DOCTYPE html>
<html lang="fr">
<head>
  <meta charset="UTF-8" />
  <title>Swagger UI - SeaDesktop</title>
  <link rel="stylesheet" href="/assets/swagger-ui/swagger-ui.css" />
  <link rel="icon" href="/assets/swagger-ui/favicon-32x32.png" />
  <style nonce=")HTML" << nonce << R"HTML(">
    html, body {
      margin: 0;
      padding: 0;
      height: 100%;
      background: #fafafa;
    }
    #swagger-ui {
      height: 100%;
    }
  </style>
</head>
<body>
  <div id="swagger-ui"></div>

  <script src="/assets/swagger-ui/swagger-ui-bundle.js"></script>
  <script src="/assets/swagger-ui/swagger-ui-standalone-preset.js"></script>
  <script nonce=")HTML" << nonce << R"HTML(">
    window.onload = () => {
      window.ui = SwaggerUIBundle({
        url: '/openapi.json',
        dom_id: '#swagger-ui',
        deepLinking: true,
        presets: [
          SwaggerUIBundle.presets.apis,
          SwaggerUIStandalonePreset
        ],
        plugins: [
          SwaggerUIBundle.plugins.DownloadUrl
        ],
        layout: 'StandaloneLayout',
        persistAuthorization: true
      });
    };
  </script>
</body>
</html>
)HTML";

    // ─── CSP strict avec nonce ───
    std::ostringstream csp;
    csp << "default-src 'self'; "
        << "script-src 'self' 'nonce-" << nonce << "'; "
        << "style-src 'self' 'nonce-" << nonce << "'; "
        << "img-src 'self' data:; "
        << "font-src 'self' data:; "
        << "connect-src 'self'; "
        << "object-src 'none'; "
        << "base-uri 'self'; "
        << "form-action 'self'; "
        << "frame-ancestors 'none'";

    rep->set_status(seastar::http::reply::status_type::ok);

    // _headers[key] = value : ecrase de maniere previsible (pas comme add_header).
    // Le SecurityHeadersMiddleware detectera ce CSP et ne le remplacera pas
    // grace a son check fail-safe.
    rep->_headers["Content-Security-Policy"] = csp.str();

    rep->write_body("text/html; charset=utf-8", html.str());

    co_return std::move(rep);
}

} // namespace sea::http::handlers::misc