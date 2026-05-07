#include "swagger_assets.h"

#include "generated/swagger_ui_css.h"
#include "generated/swagger_ui_bundle_js.h"
#include "generated/swagger_ui_standalone_preset_js.h"
#include "generated/swagger_favicon.h"

#include <unordered_map>

namespace sea::http::swagger {

namespace {

const std::unordered_map<std::string, EmbeddedAsset>& assets()
{
    static const std::unordered_map<std::string, EmbeddedAsset> kAssets = {
        {
            "/assets/swagger-ui/swagger-ui.css",
            EmbeddedAsset{
                "text/css; charset=utf-8",
                swagger_ui_css_data,
                swagger_ui_css_data_len
            }
        },
        {
            "/assets/swagger-ui/swagger-ui-bundle.js",
            EmbeddedAsset{
                "application/javascript; charset=utf-8",
                swagger_ui_bundle_js_data,
                swagger_ui_bundle_js_data_len
            }
        },
        {
            "/assets/swagger-ui/swagger-ui-standalone-preset.js",
            EmbeddedAsset{
                "application/javascript; charset=utf-8",
                swagger_ui_standalone_preset_js_data,
                swagger_ui_standalone_preset_js_data_len
            }
        },
        {
            "/assets/swagger-ui/favicon-32x32.png",
            EmbeddedAsset{
                "image/png",
                swagger_favicon_data,
                swagger_favicon_data_len
            }
        }
    };

    return kAssets;
}

} // namespace

const EmbeddedAsset* find_asset(const std::string& path) noexcept
{
    const auto& a = assets();
    const auto it = a.find(path);
    if (it == a.end()) {
        return nullptr;
    }
    return &it->second;
}

} // namespace sea::http::swagger