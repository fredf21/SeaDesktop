#include "swagger_ui_handler.h"

namespace sea::http::handlers::misc {

seastar::future<std::unique_ptr<seastar::http::reply>>
SwaggerUiHandler::handle(const seastar::sstring&,
                         std::unique_ptr<seastar::http::request>,
                         std::unique_ptr<seastar::http::reply> rep)
{
    static const std::string html = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <title>Swagger UI</title>
  <link rel="stylesheet" href="https://unpkg.com/swagger-ui-dist/swagger-ui.css" />
  <style>
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

  <script src="https://unpkg.com/swagger-ui-dist/swagger-ui-bundle.js"></script>
  <script src="https://unpkg.com/swagger-ui-dist/swagger-ui-standalone-preset.js"></script>
  <script>
    window.onload = () => {
      window.ui = SwaggerUIBundle({
        url: '/openapi.json',
        dom_id: '#swagger-ui',
        deepLinking: true,
        presets: [
          SwaggerUIBundle.presets.apis,
          SwaggerUIStandalonePreset
        ],
        layout: 'StandaloneLayout'
      });
    };
  </script>
</body>
</html>
)HTML";

    rep->set_status(seastar::http::reply::status_type::ok);
    rep->write_body("text/html; charset=utf-8", html);
    co_return std::move(rep);
}

} // namespace sea::http::handlers::misc
