#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace sea::http::swagger {

/**
 * Asset Swagger UI embedde dans le binaire.
 *
 * Les donnees sont des references constantes vers les arrays
 * generes par libs/tools/generate_swagger_assets.sh.
 */
struct EmbeddedAsset {
    std::string_view content_type;
    const unsigned char* data;
    std::size_t size;
};

/**
 * Retourne l'asset correspondant au path donne.
 *
 * Paths supportes :
 *   /assets/swagger-ui/swagger-ui.css
 *   /assets/swagger-ui/swagger-ui-bundle.js
 *   /assets/swagger-ui/swagger-ui-standalone-preset.js
 *   /assets/swagger-ui/favicon-32x32.png
 *
 * @return EmbeddedAsset si trouve, nullptr sinon.
 */
[[nodiscard]] const EmbeddedAsset* find_asset(const std::string& path) noexcept;

} // namespace sea::http::swagger