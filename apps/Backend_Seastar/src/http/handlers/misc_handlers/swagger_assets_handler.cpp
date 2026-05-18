#include "swagger_assets_handler.h"

#include "../../swagger/swagger_assets.h"   // ajuste selon ton arborescence
#include "spdlog/spdlog.h"

#include <string>

namespace sea::http::handlers::misc {

seastar::future<std::unique_ptr<seastar::http::reply>>
SwaggerAssetsHandler::handle(const seastar::sstring& path,
                             std::unique_ptr<seastar::http::request> req,
                             std::unique_ptr<seastar::http::reply> rep)
{
    spdlog::get("sea.http")->debug(
        "ASSETS path_param='{}' req_url='{}'",
        std::string(path.data(), path.size()),
        std::string(req->_url.data(), req->_url.size())
        );

    const std::string path_str = !req->_url.empty()
                                     ? std::string(req->_url.data(), req->_url.size())
                                     : std::string(path.data(), path.size());
    const auto* asset = sea::http::swagger::find_asset(path_str);

    if (asset == nullptr) {
        spdlog::get("sea.http")->warn("ASSETS NOT FOUND for path='{}'", path_str);
        rep->set_status(seastar::http::reply::status_type::not_found);
        rep->write_body("html", std::string("<h1>404 Not Found</h1>"));
        co_return std::move(rep);
    }

    spdlog::get("sea.http")->debug(
        "ASSETS FOUND asset, content_type='{}' size={}",
        asset->content_type, asset->size
        );

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