#pragma once
// sea_domain/security_scheme/cors_config.h

#include <chrono>
#include <string>
#include <vector>

#include "protocol/http_protocol/http_method.h"

namespace sea::domain::security {

// Alias au niveau du namespace : HttpMethod utilisable partout
// dans sea::domain::security sans préfixe
using HttpMethod = sea::domain::http::HttpMethod;

class CorsConfig {
public:
    // Constructeur par défaut : CORS désactivé (deny all)
    CorsConfig();

    // Builder fluide
    CorsConfig& set_allowed_origins(std::vector<std::string> origins);
    CorsConfig& add_allowed_origin(std::string origin);

    CorsConfig& set_allowed_methods(std::vector<HttpMethod> methods);
    CorsConfig& add_allowed_method(HttpMethod method);

    CorsConfig& set_allowed_headers(std::vector<std::string> headers);
    CorsConfig& add_allowed_header(std::string header);

    CorsConfig& set_exposed_headers(std::vector<std::string> headers);

    CorsConfig& set_allow_credentials(bool allow);
    CorsConfig& set_max_age(std::chrono::seconds max_age);

    // Accesseurs
    const std::vector<std::string>& allowed_origins() const;
    const std::vector<HttpMethod>& allowed_methods() const;
    const std::vector<std::string>& allowed_headers() const;
    const std::vector<std::string>& exposed_headers() const;
    bool allow_credentials() const;
    std::chrono::seconds max_age() const;

    // Helpers
    bool is_enabled() const;          // au moins une origin déclarée
    bool allows_origin(const std::string& origin) const;
    bool is_wildcard() const;         // contient "*"

    // Validation
    void validate() const;

    // Factories
    static CorsConfig deny_all();
    static CorsConfig allow_all();    // pour dev uniquement, à éviter en prod
    static CorsConfig permissive(std::vector<std::string> origins);

private:
    std::vector<std::string> allowed_origins_;
    std::vector<HttpMethod> allowed_methods_;
    std::vector<std::string> allowed_headers_;
    std::vector<std::string> exposed_headers_;
    bool allow_credentials_;
    std::chrono::seconds max_age_;
};

} // namespace sea::domain::security