#include "rate_limit_rule.h"

#include <stdexcept>
#include <string>

namespace sea::domain::security {

using namespace std::chrono_literals;

// ===== Conversion string -> enum =====

RateLimitScope scope_from_string(std::string_view s)
{
    if (s == "per_ip")      return RateLimitScope::PerIp;
    if (s == "per_user")    return RateLimitScope::PerUser;
    if (s == "per_api_key") return RateLimitScope::PerApiKey;
    if (s == "global")      return RateLimitScope::Global;
    throw std::invalid_argument("RateLimitRule: unknown scope '" + std::string(s) + "'");
}

// ===== Constructeurs =====

RateLimitRule::RateLimitRule()
    : scope_(RateLimitScope::PerIp)
    , requests_(0)
    , window_(0s)
    , burst_(0)
{
}

RateLimitRule::RateLimitRule(RateLimitScope scope,
                             std::uint32_t requests,
                             std::chrono::seconds window,
                             std::uint32_t burst)
    : scope_(scope)
    , requests_(requests)
    , window_(window)
    , burst_(burst)
{
}

// ===== Setters =====

RateLimitRule& RateLimitRule::set_scope(RateLimitScope scope)
{
    scope_ = scope;
    return *this;
}

RateLimitRule& RateLimitRule::set_requests(std::uint32_t requests)
{
    requests_ = requests;
    return *this;
}

RateLimitRule& RateLimitRule::set_window(std::chrono::seconds window)
{
    window_ = window;
    return *this;
}

RateLimitRule& RateLimitRule::set_burst(std::uint32_t burst)
{
    burst_ = burst;
    return *this;
}

// ===== Getters =====

RateLimitScope RateLimitRule::scope() const { return scope_; }
std::uint32_t RateLimitRule::requests() const { return requests_; }
std::chrono::seconds RateLimitRule::window() const { return window_; }
std::uint32_t RateLimitRule::burst() const { return burst_; }

double RateLimitRule::refill_rate_per_second() const
{
    if (window_.count() <= 0) {
        return 0.0;
    }
    return static_cast<double>(requests_) / static_cast<double>(window_.count());
}

// ===== Validation =====

void RateLimitRule::validate() const
{
    if (requests_ == 0) {
        throw std::invalid_argument(
            "RateLimitRule: requests must be > 0"
            );
    }

    if (window_.count() <= 0) {
        throw std::invalid_argument(
            "RateLimitRule: window must be > 0 seconds"
            );
    }

    if (window_ > std::chrono::hours(24)) {
        throw std::invalid_argument(
            "RateLimitRule: window exceeds 24h, likely a misconfiguration"
            );
    }

    if (burst_ < requests_) {
        throw std::invalid_argument(
            "RateLimitRule: burst must be >= requests "
            "(burst is the max accumulation, requests is the steady rate)"
            );
    }

    // Limite de bon sens : 1 million de req/s par scope, c'est déjà énorme
    if (refill_rate_per_second() > 1'000'000.0) {
        throw std::invalid_argument(
            "RateLimitRule: rate exceeds 1M requests/sec, likely a misconfiguration"
            );
    }
}

// ===== Factories =====

RateLimitRule RateLimitRule::per_ip(std::uint32_t requests,
                                    std::chrono::seconds window,
                                    std::uint32_t burst)
{
    return RateLimitRule(RateLimitScope::PerIp, requests, window, burst);
}

RateLimitRule RateLimitRule::per_user(std::uint32_t requests,
                                      std::chrono::seconds window,
                                      std::uint32_t burst)
{
    return RateLimitRule(RateLimitScope::PerUser, requests, window, burst);
}

RateLimitRule RateLimitRule::global(std::uint32_t requests,
                                    std::chrono::seconds window,
                                    std::uint32_t burst)
{
    return RateLimitRule(RateLimitScope::Global, requests, window, burst);
}

} // namespace sea::domain::security