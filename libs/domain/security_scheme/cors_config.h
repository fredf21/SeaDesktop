#pragma once

// sea_domain/security/CorsConfig.hpp
#include <chrono>
#include "protocol/http_protocol/http_method.h"

namespace sea::domain::security {
using HttpMethod = sea::domain::http::HttpMethod;

class CorsConfig {
public:
    std::vector<std::string> allowed_origins() const;
    std::vector<HttpMethod> allowed_methods() const;
    std::vector<std::string> allowed_headers() const;
    bool allow_credentials() const;
    std::chrono::seconds max_age() const;

private:
    std::vector<std::string> allowed_origins_;
    std::vector<HttpMethod> allowed_methods_;
    std::vector<std::string> allowed_headers_;
    bool allow_credentials_;
    std::chrono::seconds max_age_;
};
}
