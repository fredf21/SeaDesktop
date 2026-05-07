#include "swagger_assets_handler.h"

#include "../../swagger/swagger_assets.h"   // ajuste selon ton arborescence

#include <string>

namespace sea::http::handlers::misc {

seastar::future<std::unique_ptr<seastar::http::reply>>
SwaggerAssetsHandler::handle(const seastar::sstring& path,
                             std::unique_ptr<seastar::http::request> req,
                             std::unique_ptr<seastar::http::reply> rep)
{
    std::cerr << "[ASSETS_HANDLER] path_param='" << std::string(path.data(), path.size())
    << "' req_url='" << std::string(req->_url.data(), req->_url.size())
    << "'\n";
    const std::string path_str = !req->_url.empty()
                                     ? std::string(req->_url.data(), req->_url.size())   //"/assets/swagger-ui/swagger-ui.css"
                                     : std::string(path.data(), path.size());
    const auto* asset = sea::http::swagger::find_asset(path_str);   //trouve l'asset

    if (asset == nullptr) {
                std::cerr << "[ASSETS_HANDLER] NOT FOUND for path='" << path_str << "'\n";
        rep->set_status(seastar::http::reply::status_type::not_found);
        rep->write_body("html", std::string("<h1>404 Not Found</h1>"));
        co_return std::move(rep);
    }
    std::cerr << "[ASSETS_HANDLER] FOUND asset, content_type='"
              << asset->content_type << "' size=" << asset->size << "\n";
    // Construit le body depuis les bytes embeddes
    std::string body(
        reinterpret_cast<const char*>(asset->data),
        asset->size
        );

    rep->set_status(seastar::http::reply::status_type::ok);

    // Bypass de write_body() qui ecrase le Content-Type avec application/octet-stream.
    // On ecrit directement dans _content et on appelle done() pour finaliser.
    rep->_headers["Content-Type"] = std::string(asset->content_type);
    rep->_headers["Cache-Control"] = "public, max-age=31536000, immutable";
    rep->_content = std::move(body);

    co_return std::move(rep);
}

} // namespace sea::http::handlers::misc