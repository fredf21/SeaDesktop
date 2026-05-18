#include "cookie_helper.h"

#include <cctype>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace sea::http::utils {

namespace {

// ─────────────────────────────────────────────────────────────────
// Helpers internes
// ─────────────────────────────────────────────────────────────────

/**
 * Verifie qu'un nom de cookie est conforme RFC 6265 :
 * - non vide
 * - pas d'espaces, de controles, ni des separateurs HTTP
 *
 * RFC 6265 token: 1*<any CHAR except CTLs or separators>
 * Separators (RFC 2616) : ( ) < > @ , ; : \ " / [ ] ? = { } SP HT
 *
 * Note : pour la valeur, on tolere plus large (les JWT contiennent des
 * '.' et '_' qui sont OK ; le caractere problematique principal est
 * ';' qui sert de separateur entre attributs).
 */
[[nodiscard]] bool is_valid_cookie_name(std::string_view name) noexcept
{
    if (name.empty()) return false;
    for (char c : name) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc <= 0x20 || uc == 0x7F) return false;     // controles + espace
        switch (c) {
        case '(': case ')': case '<': case '>': case '@':
        case ',': case ';': case ':': case '\\': case '"':
        case '/': case '[': case ']': case '?': case '=':
        case '{': case '}':
            return false;
        default:
            break;
        }
    }
    return true;
}

/**
 * Verifie qu'une valeur de cookie est saine.
 *
 * On rejette uniquement ';' (separateur) et '\n', '\r' (injection de
 * header). Les autres caracteres sont laisses passer car beaucoup de
 * valeurs reelles (JWT, base64 URL-safe) en contiennent legitimement.
 *
 * Pour une securite extreme, on pourrait imposer une whitelist plus
 * stricte, mais ca casserait les JWT.
 */
[[nodiscard]] bool is_safe_cookie_value(std::string_view value) noexcept
{
    for (char c : value) {
        if (c == ';' || c == '\n' || c == '\r') return false;
    }
    return true;
}

/**
 * Trim leading/trailing whitespace d'un string_view (sans alloc).
 */
[[nodiscard]] std::string_view trim_view(std::string_view sv) noexcept
{
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) {
        sv.remove_suffix(1);
    }
    return sv;
}

[[nodiscard]] std::string_view same_site_token(
    sea::domain::security::SameSitePolicy p) noexcept
{
    using sea::domain::security::SameSitePolicy;
    switch (p) {
    case SameSitePolicy::Lax:    return "Lax";
    case SameSitePolicy::Strict: return "Strict";
    case SameSitePolicy::None:   return "None";
    }
    return "Lax";   // fallback safe
}

} // namespace anonyme


// ═════════════════════════════════════════════════════════════════════
// CookieBuilder
// ═════════════════════════════════════════════════════════════════════

CookieBuilder::CookieBuilder(std::string name, std::string value)
    : name_(std::move(name))
    , value_(std::move(value))
{
}

CookieBuilder& CookieBuilder::with_domain(std::string v)
{
    domain_ = std::move(v);
    return *this;
}

CookieBuilder& CookieBuilder::with_path(std::string v)
{
    path_ = std::move(v);
    return *this;
}

CookieBuilder& CookieBuilder::with_max_age(std::chrono::seconds v)
{
    max_age_ = v;
    return *this;
}

CookieBuilder& CookieBuilder::with_secure(bool v)
{
    secure_ = v;
    return *this;
}

CookieBuilder& CookieBuilder::with_http_only(bool v)
{
    http_only_ = v;
    return *this;
}

CookieBuilder& CookieBuilder::with_same_site(sea::domain::security::SameSitePolicy v)
{
    same_site_ = v;
    return *this;
}

CookieBuilder& CookieBuilder::as_expired()
{
    value_.clear();
    max_age_ = std::chrono::seconds(0);
    return *this;
}

std::string CookieBuilder::build() const
{
    if (!is_valid_cookie_name(name_)) {
        throw std::invalid_argument(
            "CookieBuilder: invalid cookie name '" + name_ +
            "' (RFC 6265 token: no controls, spaces, or HTTP separators)"
            );
    }
    if (!is_safe_cookie_value(value_)) {
        throw std::invalid_argument(
            "CookieBuilder: cookie value contains forbidden characters "
            "(';', '\\n' or '\\r') — possible header injection attempt"
            );
    }

    std::ostringstream out;
    out << name_ << '=' << value_;

    if (max_age_.has_value()) {
        out << "; Max-Age=" << max_age_->count();
    }

    if (!domain_.empty()) {
        out << "; Domain=" << domain_;
    }

    if (!path_.empty()) {
        out << "; Path=" << path_;
    }

    if (secure_) {
        out << "; Secure";
    }

    if (http_only_) {
        out << "; HttpOnly";
    }

    out << "; SameSite=" << same_site_token(same_site_);

    return out.str();
}


// ═════════════════════════════════════════════════════════════════════
// Helpers haut niveau
// ═════════════════════════════════════════════════════════════════════

std::string build_access_cookie(
    const sea::domain::security::CookieConfig& config,
    const std::string& token,
    std::chrono::seconds ttl)
{
    CookieBuilder builder(config.access_token_name(), token);

    builder.with_path(config.path())
        .with_secure(config.secure())
        .with_http_only(config.http_only())
        .with_same_site(config.same_site())
        .with_max_age(ttl);

    if (!config.domain().empty()) {
        builder.with_domain(config.domain());
    }

    return builder.build();
}

std::string build_refresh_cookie(
    const sea::domain::security::CookieConfig& config,
    const std::string& token,
    std::chrono::seconds ttl)
{
    CookieBuilder builder(config.refresh_token_name(), token);

    builder.with_path(config.path())
        .with_secure(config.secure())
        .with_http_only(config.http_only())
        .with_same_site(config.same_site())
        .with_max_age(ttl);

    if (!config.domain().empty()) {
        builder.with_domain(config.domain());
    }

    return builder.build();
}

std::string clear_access_cookie(const sea::domain::security::CookieConfig& config)
{
    // Important : tous les attributs SAUF Max-Age et la valeur doivent
    // matcher le cookie d'origine, sinon le navigateur ne le supprime pas.
    CookieBuilder builder(config.access_token_name(), "");
    builder.with_path(config.path())
        .with_secure(config.secure())
        .with_http_only(config.http_only())
        .with_same_site(config.same_site())
        .as_expired();

    if (!config.domain().empty()) {
        builder.with_domain(config.domain());
    }

    return builder.build();
}

std::string clear_refresh_cookie(const sea::domain::security::CookieConfig& config)
{
    CookieBuilder builder(config.refresh_token_name(), "");
    builder.with_path(config.path())
        .with_secure(config.secure())
        .with_http_only(config.http_only())
        .with_same_site(config.same_site())
        .as_expired();

    if (!config.domain().empty()) {
        builder.with_domain(config.domain());
    }

    return builder.build();
}


// ═════════════════════════════════════════════════════════════════════
// Parsing du header Cookie:
// ═════════════════════════════════════════════════════════════════════

std::unordered_map<std::string, std::string>
parse_cookie_header(std::string_view header_value)
{
    std::unordered_map<std::string, std::string> result;

    std::size_t start = 0;
    while (start < header_value.size()) {
        // Trouve le prochain ';' qui separe les cookies
        const std::size_t sep = header_value.find(';', start);
        const std::size_t end = (sep == std::string_view::npos)
                                    ? header_value.size()
                                    : sep;

        // Extrait "name=value" trim
        const std::string_view pair = trim_view(header_value.substr(start, end - start));

        if (!pair.empty()) {
            const std::size_t eq = pair.find('=');
            if (eq != std::string_view::npos) {
                const std::string_view name  = trim_view(pair.substr(0, eq));
                const std::string_view value = trim_view(pair.substr(eq + 1));

                if (!name.empty()) {
                    result.insert_or_assign(
                        std::string(name),
                        std::string(value)
                        );
                }
            }
            // Cookies sans '=' sont ignores silencieusement
        }

        if (sep == std::string_view::npos) {
            break;
        }
        start = sep + 1;
    }

    return result;
}

std::optional<std::string>
get_cookie_value(std::string_view header_value, std::string_view cookie_name)
{
    // Recherche optimisee : parcourt sans construire toute la map.
    // Si plusieurs cookies du meme nom, retourne le dernier (semantique
    // coherente avec parse_cookie_header).
    std::optional<std::string> last_match;

    std::size_t start = 0;
    while (start < header_value.size()) {
        const std::size_t sep = header_value.find(';', start);
        const std::size_t end = (sep == std::string_view::npos)
                                    ? header_value.size()
                                    : sep;

        const std::string_view pair = trim_view(header_value.substr(start, end - start));
        if (!pair.empty()) {
            const std::size_t eq = pair.find('=');
            if (eq != std::string_view::npos) {
                const std::string_view name = trim_view(pair.substr(0, eq));
                if (name == cookie_name) {
                    const std::string_view value = trim_view(pair.substr(eq + 1));
                    last_match = std::string(value);
                }
            }
        }

        if (sep == std::string_view::npos) {
            break;
        }
        start = sep + 1;
    }

    return last_match;
}

} // namespace sea::http::utils