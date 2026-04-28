#include "cors_config.h"

#include <algorithm>
#include <stdexcept>

namespace sea::domain::security {

using namespace std::chrono_literals;

// ===== Constructeur =====

CorsConfig::CorsConfig()
    : allowed_origins_{}
    , allowed_methods_{}
    , allowed_headers_{}
    , exposed_headers_{}
    , allow_credentials_(false)
    , max_age_(0s)
{
}

// ===== Setters =====

CorsConfig& CorsConfig::set_allowed_origins(std::vector<std::string> origins)
{
    allowed_origins_ = std::move(origins);
    return *this;
}

CorsConfig& CorsConfig::add_allowed_origin(std::string origin)
{
    allowed_origins_.push_back(std::move(origin));
    return *this;
}

CorsConfig& CorsConfig::set_allowed_methods(std::vector<HttpMethod> methods)
{
    allowed_methods_ = std::move(methods);
    return *this;
}

CorsConfig& CorsConfig::add_allowed_method(HttpMethod method)
{
    allowed_methods_.push_back(method);
    return *this;
}

CorsConfig& CorsConfig::set_allowed_headers(std::vector<std::string> headers)
{
    allowed_headers_ = std::move(headers);
    return *this;
}

CorsConfig& CorsConfig::add_allowed_header(std::string header)
{
    allowed_headers_.push_back(std::move(header));
    return *this;
}

CorsConfig& CorsConfig::set_exposed_headers(std::vector<std::string> headers)
{
    exposed_headers_ = std::move(headers);
    return *this;
}

CorsConfig& CorsConfig::set_allow_credentials(bool allow)
{
    allow_credentials_ = allow;
    return *this;
}

CorsConfig& CorsConfig::set_max_age(std::chrono::seconds max_age)
{
    max_age_ = max_age;
    return *this;
}

// ===== Getters =====

const std::vector<std::string>& CorsConfig::allowed_origins() const
{
    return allowed_origins_;
}

const std::vector<HttpMethod>& CorsConfig::allowed_methods() const
{
    return allowed_methods_;
}

const std::vector<std::string>& CorsConfig::allowed_headers() const
{
    return allowed_headers_;
}

const std::vector<std::string>& CorsConfig::exposed_headers() const
{
    return exposed_headers_;
}

bool CorsConfig::allow_credentials() const
{
    return allow_credentials_;
}

std::chrono::seconds CorsConfig::max_age() const
{
    return max_age_;
}

// ===== Helpers =====

bool CorsConfig::is_enabled() const
{
    return !allowed_origins_.empty();
}

bool CorsConfig::is_wildcard() const
{
    return std::find(allowed_origins_.begin(),
                     allowed_origins_.end(),
                     "*") != allowed_origins_.end();
}

bool CorsConfig::allows_origin(const std::string& origin) const
{
    // Wildcard accepte tout
    if (is_wildcard()) {
        return true;
    }
    // Sinon, match exact dans la liste
    return std::find(allowed_origins_.begin(),
                     allowed_origins_.end(),
                     origin) != allowed_origins_.end();
}

// ===== Validation =====

void CorsConfig::validate() const
{
    // 1. Si CORS désactivé (pas d'origins), rien à valider
    if (!is_enabled()) {
        return;
    }

    // 2. allow_credentials=true interdit le wildcard "*"
    //    (règle imposée par les navigateurs, sinon les requêtes échouent)
    if (allow_credentials_ && is_wildcard()) {
        throw std::invalid_argument(
            "CorsConfig: allow_credentials=true is incompatible with "
            "wildcard origin '*'. Browsers refuse to send credentials "
            "to wildcard origins. List explicit origins instead."
            );
    }

    // 3. max_age négatif interdit
    if (max_age_.count() < 0) {
        throw std::invalid_argument(
            "CorsConfig: max_age must be >= 0"
            );
    }

    // 4. max_age trop élevé : suspect mais pas bloquant
    //    (Chrome cap à 7200s, Firefox à 86400s)
    if (max_age_ > std::chrono::hours(24)) {
        throw std::invalid_argument(
            "CorsConfig: max_age exceeds 24h. Browsers cap this value "
            "(Chrome: 2h, Firefox: 24h), so higher values have no effect."
            );
    }

    // 5. Si on a des origines mais aucune méthode, c'est une mauvaise config
    if (allowed_methods_.empty()) {
        throw std::invalid_argument(
            "CorsConfig: allowed_origins is set but allowed_methods is empty. "
            "Specify at least one HTTP method (e.g. GET)."
            );
    }
}

// ===== Factories =====

CorsConfig CorsConfig::deny_all()
{
    return CorsConfig{};  // tous les vecteurs vides = tout interdit
}

CorsConfig CorsConfig::allow_all()
{
    CorsConfig config;
    config.add_allowed_origin("*");
    config.set_allowed_methods({
        HttpMethod::Get,
        HttpMethod::Post,
        HttpMethod::Put,
        HttpMethod::Patch,
        HttpMethod::Delete,
        HttpMethod::Head,
        HttpMethod::Options
    });
    config.set_allowed_headers({"*"});
    config.set_allow_credentials(false);  // obligatoire avec wildcard
    config.set_max_age(86400s);
    return config;
}

CorsConfig CorsConfig::permissive(std::vector<std::string> origins)
{
    CorsConfig config;
    config.set_allowed_origins(std::move(origins));
    config.set_allowed_methods({
        HttpMethod::Get,
        HttpMethod::Post,
        HttpMethod::Put,
        HttpMethod::Patch,
        HttpMethod::Delete,
        HttpMethod::Options
    });
    config.set_allowed_headers({
        "Content-Type",
        "Authorization",
        "X-Requested-With"
    });
    config.set_allow_credentials(true);
    config.set_max_age(86400s);
    return config;
}

} // namespace sea::domain::security