#include "cookie_config.h"

#include <stdexcept>
#include <utility>

namespace sea::domain::security {

// ===== Conversions =====

TokenDelivery token_delivery_from_string(std::string_view s)
{
    if (s == "body")   return TokenDelivery::Body;
    if (s == "cookie") return TokenDelivery::Cookie;
    if (s == "both")   return TokenDelivery::Both;
    throw std::invalid_argument(
        "CookieConfig: unknown token_delivery '" + std::string(s) +
        "' (expected: body | cookie | both)"
        );
}

std::string_view to_string(TokenDelivery d) noexcept
{
    switch (d) {
    case TokenDelivery::Body:   return "body";
    case TokenDelivery::Cookie: return "cookie";
    case TokenDelivery::Both:   return "both";
    }
    return "unknown";
}

SameSitePolicy same_site_from_string(std::string_view s)
{
    if (s == "lax"    || s == "Lax")    return SameSitePolicy::Lax;
    if (s == "strict" || s == "Strict") return SameSitePolicy::Strict;
    if (s == "none"   || s == "None")   return SameSitePolicy::None;
    throw std::invalid_argument(
        "CookieConfig: unknown same_site '" + std::string(s) +
        "' (expected: lax | strict | none)"
        );
}

std::string_view to_string(SameSitePolicy p) noexcept
{
    switch (p) {
    case SameSitePolicy::Lax:    return "Lax";
    case SameSitePolicy::Strict: return "Strict";
    case SameSitePolicy::None:   return "None";
    }
    return "unknown";
}

// ===== Factory =====

CookieConfig CookieConfig::safe_defaults()
{
    CookieConfig cfg;
    // Tout est déjà initialisé aux valeurs sûres dans la déclaration
    return cfg;
}

// ===== Setters =====

CookieConfig& CookieConfig::set_domain(std::string v)
{
    domain_ = std::move(v);
    return *this;
}

CookieConfig& CookieConfig::set_path(std::string v)
{
    path_ = std::move(v);
    return *this;
}

CookieConfig& CookieConfig::set_secure(bool v)
{
    secure_ = v;
    return *this;
}

CookieConfig& CookieConfig::set_same_site(SameSitePolicy v)
{
    same_site_ = v;
    return *this;
}

CookieConfig& CookieConfig::set_access_token_name(std::string v)
{
    access_token_name_ = std::move(v);
    return *this;
}

CookieConfig& CookieConfig::set_refresh_token_name(std::string v)
{
    refresh_token_name_ = std::move(v);
    return *this;
}

// ===== Validation =====

void CookieConfig::validate() const
{
    if (path_.empty()) {
        throw std::invalid_argument("CookieConfig: path cannot be empty");
    }
    if (path_.front() != '/') {
        throw std::invalid_argument(
            "CookieConfig: path must start with '/' (got '" + path_ + "')"
            );
    }

    if (access_token_name_.empty()) {
        throw std::invalid_argument("CookieConfig: access_token_name cannot be empty");
    }
    if (refresh_token_name_.empty()) {
        throw std::invalid_argument("CookieConfig: refresh_token_name cannot be empty");
    }
    if (access_token_name_ == refresh_token_name_) {
        throw std::invalid_argument(
            "CookieConfig: access_token_name and refresh_token_name must differ "
            "(got both = '" + access_token_name_ + "')"
            );
    }

    // SameSite=None impose Secure=true (règle browser)
    if (same_site_ == SameSitePolicy::None && !secure_) {
        throw std::invalid_argument(
            "CookieConfig: same_site=None requires secure=true "
            "(modern browsers reject SameSite=None cookies without Secure)"
            );
    }
}

} // namespace sea::domain::security