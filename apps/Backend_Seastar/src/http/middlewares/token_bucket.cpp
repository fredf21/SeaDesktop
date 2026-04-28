#include "token_bucket.h"

#include <algorithm>
#include <cmath>

namespace sea::http::middlewares {

TokenBucket::TokenBucket(double capacity, double refill_rate_per_second)
    : capacity_(capacity)
    , refill_rate_(refill_rate_per_second)
    , tokens_(capacity)
    , last_refill_(std::chrono::steady_clock::now())
{
}

void TokenBucket::refill_now()
{
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration<double>(now - last_refill_).count();

    tokens_ = std::min(capacity_, tokens_ + elapsed * refill_rate_);
    last_refill_ = now;
}

void TokenBucket::refill()
{
    refill_now();
}

bool TokenBucket::try_consume()
{
    refill_now();
    if (tokens_ < 1.0) {
        return false;
    }
    tokens_ -= 1.0;
    return true;
}

double TokenBucket::available_now()
{
    refill_now();
    return tokens_;
}

double TokenBucket::available_raw() const
{
    return tokens_;
}

std::int64_t TokenBucket::seconds_until_next_token() const
{
    if (tokens_ >= 1.0 || refill_rate_ <= 0.0) {
        return 0;
    }
    const double seconds = (1.0 - tokens_) / refill_rate_;
    return static_cast<std::int64_t>(std::ceil(seconds));
}

} // namespace sea::http::middlewares