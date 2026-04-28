#include "security_config.h"


namespace sea::domain::security {

SecurityConfig::SecurityConfig()
    : authentication_{}
    , cors_{}
    , http_limits_{}
    , security_headers_{}
    , rate_limits_{}
{
}

// ===== Setters =====

SecurityConfig& SecurityConfig::set_authentication(AuthentificationConfig auth)
{
    authentication_ = std::move(auth);
    return *this;
}

SecurityConfig& SecurityConfig::set_cors(CorsConfig cors)
{
    cors_ = std::move(cors);
    return *this;
}

SecurityConfig& SecurityConfig::set_http_limits(HttpLimits limits)
{
    http_limits_ = std::move(limits);
    return *this;
}

SecurityConfig& SecurityConfig::set_security_headers(SecurityHeaders headers)
{
    security_headers_ = std::move(headers);
    return *this;
}

SecurityConfig& SecurityConfig::set_rate_limits(std::vector<RateLimitRule> rules)
{
    rate_limits_ = std::move(rules);
    return *this;
}

SecurityConfig& SecurityConfig::add_rate_limit(RateLimitRule rule)
{
    rate_limits_.push_back(std::move(rule));
    return *this;
}

// ===== Getters =====

const AuthentificationConfig& SecurityConfig::authentication() const
{
    return authentication_;
}

const CorsConfig& SecurityConfig::cors() const
{
    return cors_;
}

const HttpLimits& SecurityConfig::http_limits() const
{
    return http_limits_;
}

const SecurityHeaders& SecurityConfig::security_headers() const
{
    return security_headers_;
}

const std::vector<RateLimitRule>& SecurityConfig::rate_limits() const
{
    return rate_limits_;
}

// ===== Validation =====

void SecurityConfig::validate() const
{
    // 1. HttpLimits
    http_limits_.validate();

    // 2. Authentication
    authentication_.validate();

    // 3. CORS + credentials interdit le wildcard
    if (cors_.allow_credentials()) {
        for (const auto& origin : cors_.allowed_origins()) {
            if (origin == "*") {
                throw std::invalid_argument(
                    "SecurityConfig: CORS allow_credentials=true is incompatible "
                    "with wildcard origin '*'. List explicit origins instead."
                    );
            }
        }
    }

    // 4. Rate limits
    for (const auto& rule : rate_limits_) {
        rule.validate();
    }
}

// ===== Factories =====

SecurityConfig SecurityConfig::safe_defaults()
{
    SecurityConfig config;
    config.set_http_limits(HttpLimits::safe_defaults());
    config.set_security_headers(SecurityHeaders::recommended());
    return config;
}

SecurityConfig SecurityConfig::disabled()
{
    SecurityConfig config;
    config.set_security_headers(SecurityHeaders::none());
    return config;
}

} // namespace sea::domain::security