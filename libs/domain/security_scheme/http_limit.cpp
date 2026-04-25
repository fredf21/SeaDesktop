#include "http_limit.h"

#include <stdexcept>

namespace sea::domain::security {

using namespace std::chrono_literals;

HttpLimits::HttpLimits()
    : max_body_size_(1 * 1024 * 1024)
    , max_header_size_(8 * 1024)
    , max_headers_count_(100)
    , max_url_length_(2 * 1024)
    , max_query_params_(100)
    , request_timeout_(30s)
    , keep_alive_timeout_(60s)
    , max_connections_per_ip_(100)
{
}

HttpLimits& HttpLimits::set_max_body_size(std::uint64_t bytes)
{
    max_body_size_ = bytes;
    return *this;
}

HttpLimits& HttpLimits::set_max_header_size(std::uint64_t bytes)
{
    max_header_size_ = bytes;
    return *this;
}

HttpLimits& HttpLimits::set_max_headers_count(std::uint32_t count)
{
    max_headers_count_ = count;
    return *this;
}

HttpLimits& HttpLimits::set_max_url_length(std::uint64_t bytes)
{
    max_url_length_ = bytes;
    return *this;
}

HttpLimits& HttpLimits::set_max_query_params(std::uint32_t count)
{
    max_query_params_ = count;
    return *this;
}

HttpLimits& HttpLimits::set_request_timeout(std::chrono::seconds timeout)
{
    request_timeout_ = timeout;
    return *this;
}

HttpLimits& HttpLimits::set_keep_alive_timeout(std::chrono::seconds timeout)
{
    keep_alive_timeout_ = timeout;
    return *this;
}

HttpLimits& HttpLimits::set_max_connections_per_ip(std::uint32_t count)
{
    max_connections_per_ip_ = count;
    return *this;
}

std::uint64_t HttpLimits::max_body_size() const { return max_body_size_; }
std::uint64_t HttpLimits::max_header_size() const { return max_header_size_; }
std::uint32_t HttpLimits::max_headers_count() const { return max_headers_count_; }
std::uint64_t HttpLimits::max_url_length() const { return max_url_length_; }
std::uint32_t HttpLimits::max_query_params() const { return max_query_params_; }
std::chrono::seconds HttpLimits::request_timeout() const { return request_timeout_; }
std::chrono::seconds HttpLimits::keep_alive_timeout() const { return keep_alive_timeout_; }
std::uint32_t HttpLimits::max_connections_per_ip() const { return max_connections_per_ip_; }

void HttpLimits::validate() const
{
    if (max_body_size_ == 0) {
        throw std::invalid_argument("HttpLimits: max_body_size must be > 0");
    }
    if (max_body_size_ > 100ULL * 1024 * 1024 * 1024) {
        throw std::invalid_argument("HttpLimits: max_body_size exceeds reasonable limit (100GB)");
    }
    if (max_url_length_ == 0 || max_url_length_ > 1024 * 1024) {
        throw std::invalid_argument("HttpLimits: max_url_length out of range (1B - 1MB)");
    }
    if (request_timeout_.count() <= 0) {
        throw std::invalid_argument("HttpLimits: request_timeout must be positive");
    }
    if (request_timeout_ > std::chrono::hours(24)) {
        throw std::invalid_argument("HttpLimits: request_timeout exceeds 24h, likely a bug");
    }
    if (max_headers_count_ == 0) {
        throw std::invalid_argument("HttpLimits: max_headers_count must be > 0");
    }
    if (max_connections_per_ip_ == 0) {
        throw std::invalid_argument("HttpLimits: max_connections_per_ip must be > 0");
    }
}

HttpLimits HttpLimits::safe_defaults()
{
    return HttpLimits{};
}

} // namespace sea::domain::security