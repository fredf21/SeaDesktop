#pragma once

#include <string>

namespace sea::domain::security {

/**
 * @brief Mode de livraison des tokens JWT au client.
 *
 * - Body    : tokens dans le corps JSON de la réponse (compatible API/CLI)
 * - Cookie  : tokens dans des cookies HttpOnly (navigateur, anti-XSS)
 * - Both    : les deux à la fois (flexibilité maximale, défaut recommandé)
 */
enum class TokenDelivery {
    Body,
    Cookie,
    Both
};

/// "body" | "cookie" | "both"  →  enum.
TokenDelivery token_delivery_from_string(std::string_view s);
std::string_view to_string(TokenDelivery d) noexcept;


/**
 * @brief Stratégie SameSite pour les cookies.
 *
 * - Lax    : cookie envoyé sur navigations top-level GET (défaut sain anti-CSRF)
 * - Strict : cookie jamais envoyé en cross-site (très strict, peut casser des liens)
 * - None   : cookie envoyé partout (nécessite Secure=true et CORS bien configuré)
 */
enum class SameSitePolicy {
    Lax,
    Strict,
    None
};

SameSitePolicy same_site_from_string(std::string_view s);
std::string_view to_string(SameSitePolicy p) noexcept;


/**
 * @brief Configuration des cookies HttpOnly pour livrer les tokens JWT.
 *
 * Tous les attributs sont configurables sauf HttpOnly qui est toujours true
 * (non négociable : c'est ce qui protège contre l'accès JS et donc XSS).
 *
 * Configuration YAML :
 *
 *   cookie:
 *     domain: ""                      # optionnel, défaut: omis = domaine de la requête
 *     path: "/"                        # défaut "/"
 *     secure: true                     # défaut true (HTTPS only)
 *     same_site: lax                   # lax | strict | none (défaut lax)
 *     access_token_name: "sea_access"  # nom du cookie d'access
 *     refresh_token_name: "sea_refresh"
 */
class CookieConfig {
public:
    // ─── Constructeurs / Factory ─────────────────────────────────
    CookieConfig() = default;

    /// Valeurs par défaut sûres pour la production (Secure=true, SameSite=Lax)
    [[nodiscard]] static CookieConfig safe_defaults();

    // ─── Builder fluide ──────────────────────────────────────────
    CookieConfig& set_domain(std::string v);
    CookieConfig& set_path(std::string v);
    CookieConfig& set_secure(bool v);
    CookieConfig& set_same_site(SameSitePolicy v);
    CookieConfig& set_access_token_name(std::string v);
    CookieConfig& set_refresh_token_name(std::string v);

    // ─── Accesseurs ──────────────────────────────────────────────
    [[nodiscard]] const std::string& domain()              const noexcept { return domain_; }
    [[nodiscard]] const std::string& path()                const noexcept { return path_; }
    [[nodiscard]] bool               secure()              const noexcept { return secure_; }
    [[nodiscard]] SameSitePolicy     same_site()           const noexcept { return same_site_; }
    [[nodiscard]] const std::string& access_token_name()   const noexcept { return access_token_name_; }
    [[nodiscard]] const std::string& refresh_token_name()  const noexcept { return refresh_token_name_; }

    // HttpOnly toujours true (non configurable, sécurité)
    [[nodiscard]] constexpr bool http_only() const noexcept { return true; }

    // ─── Validation ──────────────────────────────────────────────
    /**
     * @throws std::invalid_argument si la configuration est incohérente.
     * - path non vide
     * - access_token_name et refresh_token_name non vides et différents
     * - SameSite=None implique Secure=true (règle browser stricte)
     */
    void validate() const;

private:
    std::string    domain_;                       // vide = omis dans Set-Cookie
    std::string    path_                = "/";
    bool           secure_              = true;
    SameSitePolicy same_site_           = SameSitePolicy::Lax;
    std::string    access_token_name_   = "sea_access";
    std::string    refresh_token_name_  = "sea_refresh";
};

} // namespace sea::domain::security
