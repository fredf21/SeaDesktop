#pragma once
// sea_domain/security_scheme/security_config.h

#include <vector>

#include "authentification_config.h"
#include "cors_config.h"
#include "http_limit.h"
#include "rate_limit_rule.h"
#include "security_headers.h"
#include "security_scheme/http_limit.h"

namespace sea::domain::security {

class SecurityConfig {
public:
    SecurityConfig();

    SecurityConfig& set_authentication(AuthentificationConfig auth);
    SecurityConfig& set_cors(CorsConfig cors);
    SecurityConfig& set_http_limits(HttpLimits limits);
    SecurityConfig& set_security_headers(SecurityHeaders headers);
    SecurityConfig& set_rate_limits(std::vector<RateLimitRule> rules);
    SecurityConfig& add_rate_limit(RateLimitRule rule);

    const AuthentificationConfig& authentication() const;
    const CorsConfig& cors() const;
    const HttpLimits& http_limits() const;
    const SecurityHeaders& security_headers() const;
    const std::vector<RateLimitRule>& rate_limits() const;

    void validate() const;

    static SecurityConfig safe_defaults();
    static SecurityConfig disabled();

private:
    AuthentificationConfig authentication_;
    CorsConfig cors_;
    HttpLimits http_limits_;
    SecurityHeaders security_headers_;
    std::vector<RateLimitRule> rate_limits_;
};

} // namespace sea::domain::security