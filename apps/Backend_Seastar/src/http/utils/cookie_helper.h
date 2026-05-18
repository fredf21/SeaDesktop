#pragma once

#include "security_scheme/cookie_config.h"

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace sea::http::utils {

// ─────────────────────────────────────────────────────────────────────
// CookieBuilder
//
// API fluide pour construire la valeur d'un header Set-Cookie.
//
// Exemple :
//
//   const std::string header_value = CookieBuilder("sea_access", token)
//       .with_max_age(std::chrono::minutes(15))
//       .with_path("/")
//       .with_secure(true)
//       .with_http_only(true)
//       .with_same_site(sea::domain::security::SameSitePolicy::Lax)
//       .build();
//
//   // header_value = "sea_access=<token>; Max-Age=900; Path=/; Secure; HttpOnly; SameSite=Lax"
//
// Le builder ne gere PAS le prefixe "Set-Cookie:" lui-meme — il produit
// uniquement la valeur du header. C'est au caller d'ajouter l'header
// au reply Seastar via reply->add_header("Set-Cookie", value).
//
// Pour effacer un cookie cote navigateur : utiliser ClearCookie ou
// passer un Max-Age = 0 au builder. Voir helpers en bas du fichier.
// ─────────────────────────────────────────────────────────────────────
class CookieBuilder {
public:
    /**
     * Cree un builder avec le nom et la valeur du cookie.
     *
     * @param name   Nom du cookie (ex: "sea_access")
     * @param value  Valeur (ex: le token JWT brut). Sera echappee si necessaire.
     *
     * Defauts initiaux :
     *   - path = "/"
     *   - secure = true
     *   - http_only = true (par defaut pour tokens — securite)
     *   - same_site = Lax
     *   - max_age, domain, expires : non definis (omis du header)
     */
    CookieBuilder(std::string name, std::string value);

    // ─── Setters fluides ─────────────────────────────────────────
    CookieBuilder& with_domain(std::string v);
    CookieBuilder& with_path(std::string v);
    CookieBuilder& with_max_age(std::chrono::seconds v);
    CookieBuilder& with_secure(bool v);
    CookieBuilder& with_http_only(bool v);
    CookieBuilder& with_same_site(sea::domain::security::SameSitePolicy v);

    /**
     * Configure le builder pour qu'il EFFACE le cookie cote navigateur.
     * Equivaut a Max-Age=0 (et valeur vide), ce qui invite le navigateur
     * a supprimer le cookie immediatement.
     */
    CookieBuilder& as_expired();

    // ─── Build ───────────────────────────────────────────────────
    /**
     * Produit la valeur du header Set-Cookie.
     *
     * Exemple de sortie :
     *   "sea_access=<token>; Max-Age=900; Path=/; Secure; HttpOnly; SameSite=Lax"
     *
     * @throws std::invalid_argument si le nom contient des caracteres invalides
     *         (controle, espace, separateurs HTTP).
     */
    [[nodiscard]] std::string build() const;

private:
    std::string                    name_;
    std::string                    value_;
    std::string                    domain_;       // vide = omis
    std::string                    path_         = "/";
    std::optional<std::chrono::seconds> max_age_;  // nullopt = omis
    bool                           secure_        = true;
    bool                           http_only_     = true;
    sea::domain::security::SameSitePolicy same_site_
        = sea::domain::security::SameSitePolicy::Lax;
};


// ─────────────────────────────────────────────────────────────────────
// Helpers haut niveau
//
// Conveniences qui prennent une CookieConfig (du domaine) et produisent
// directement le header pret a l'emploi. Utilises par login/refresh
// handlers pour eviter de re-configurer le builder a chaque fois.
// ─────────────────────────────────────────────────────────────────────

/**
 * Construit le cookie d'access token a partir de la CookieConfig
 * du service. La valeur du Max-Age est imposee par le TTL passe.
 *
 * @param config  Configuration cookies du service (depuis le YAML)
 * @param token   Token JWT brut a poser dans le cookie
 * @param ttl     Duree de vie du cookie (= TTL du JWT)
 *
 * Equivalent a :
 *   CookieBuilder(config.access_token_name(), token)
 *     .with_max_age(ttl)
 *     .with_path(config.path())
 *     ...
 */
[[nodiscard]] std::string build_access_cookie(
    const sea::domain::security::CookieConfig& config,
    const std::string& token,
    std::chrono::seconds ttl);

/**
 * Idem pour le refresh token (utilise refresh_token_name).
 */
[[nodiscard]] std::string build_refresh_cookie(
    const sea::domain::security::CookieConfig& config,
    const std::string& token,
    std::chrono::seconds ttl);

/**
 * Construit un cookie "d'expiration" pour effacer le cookie d'access
 * cote navigateur (logout).
 *
 * Le builder pose Max-Age=0 + valeur vide. Le navigateur supprime alors
 * le cookie. Tous les autres attributs (Path, Domain, Secure, SameSite)
 * doivent EXACTEMENT correspondre au cookie d'origine, sinon le
 * navigateur ne le supprimera pas (il considererait que c'est un autre
 * cookie). On utilise donc la config du service pour reconstruire ces
 * attributs.
 */
[[nodiscard]] std::string clear_access_cookie(
    const sea::domain::security::CookieConfig& config);

/**
 * Idem pour le refresh.
 */
[[nodiscard]] std::string clear_refresh_cookie(
    const sea::domain::security::CookieConfig& config);


// ─────────────────────────────────────────────────────────────────────
// Parsing du header Cookie: cote serveur (lecture des cookies entrants)
// ─────────────────────────────────────────────────────────────────────

/**
 * Parse la valeur d'un header "Cookie:" envoye par le client.
 *
 * Exemple :
 *   parse_cookie_header("sea_access=abc; theme=dark; lang=fr")
 *     -> { {"sea_access","abc"}, {"theme","dark"}, {"lang","fr"} }
 *
 * Caracteristiques :
 *   - Tolerant aux espaces autour des '=' et ';'
 *   - Si une cle apparait plusieurs fois, le dernier gagne
 *   - Les cookies sans '=' sont ignores (entree malformee)
 *
 * @param header_value  Valeur du header (ex: req.get_header("Cookie"))
 * @return  Map nom -> valeur
 */
[[nodiscard]] std::unordered_map<std::string, std::string>
parse_cookie_header(std::string_view header_value);

/**
 * Convenience : recupere directement la valeur d'un cookie par nom.
 *
 * @return std::nullopt si le cookie n'est pas present.
 */
[[nodiscard]] std::optional<std::string>
get_cookie_value(std::string_view header_value, std::string_view cookie_name);

} // namespace sea::http::utils
