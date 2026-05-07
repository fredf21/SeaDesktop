#pragma once

#include <string>

namespace sea::http::utils {

/**
 * Genere un nonce cryptographiquement securise pour CSP.
 *
 * Utilise std::random_device + mt19937_64 seede.
 * Pour notre usage (nonce CSP de courte duree de vie), c'est suffisant.
 *
 * @return Une chaine de 24 caracteres URL-safe.
 *         Ex: "K9j2L3mN4pQ5rS6tU7v8WxYz"
 */
[[nodiscard]] std::string generate_csp_nonce();

} // namespace sea::http::utils