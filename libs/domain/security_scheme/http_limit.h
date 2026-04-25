#pragma once
// sea_domain/security_scheme/http_limits.h

#include <chrono>
#include <cstdint>

namespace sea::domain::security {

class HttpLimits {
public:
    HttpLimits();

    HttpLimits& set_max_body_size(std::uint64_t bytes);
    HttpLimits& set_max_header_size(std::uint64_t bytes);
    HttpLimits& set_max_headers_count(std::uint32_t count);
    HttpLimits& set_max_url_length(std::uint64_t bytes);
    HttpLimits& set_max_query_params(std::uint32_t count);
    HttpLimits& set_request_timeout(std::chrono::seconds timeout);
    HttpLimits& set_keep_alive_timeout(std::chrono::seconds timeout);
    HttpLimits& set_max_connections_per_ip(std::uint32_t count);

    std::uint64_t max_body_size() const;
    std::uint64_t max_header_size() const;
    std::uint32_t max_headers_count() const;
    std::uint64_t max_url_length() const;
    std::uint32_t max_query_params() const;
    std::chrono::seconds request_timeout() const;
    std::chrono::seconds keep_alive_timeout() const;
    std::uint32_t max_connections_per_ip() const;

    void validate() const;

    static HttpLimits safe_defaults();

private:
    std::uint64_t max_body_size_;
    std::uint64_t max_header_size_;
    std::uint32_t max_headers_count_;
    std::uint64_t max_url_length_;
    std::uint32_t max_query_params_;
    std::chrono::seconds request_timeout_;
    std::chrono::seconds keep_alive_timeout_;
    std::uint32_t max_connections_per_ip_;
};

} // namespace sea::domain::security