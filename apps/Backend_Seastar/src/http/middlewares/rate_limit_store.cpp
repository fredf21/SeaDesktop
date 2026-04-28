#include "rate_limit_store.h"

#include <seastar/core/smp.hh>

#include <functional>

namespace sea::http::middlewares {

unsigned RateLimitStore::shard_of_key(const std::string& key)
{
    // Hash stable de la clé, modulo le nombre de shards.
    // Garantit que la même clé tombe toujours sur le même shard.
    const std::size_t h = std::hash<std::string>{}(key);
    return static_cast<unsigned>(h % seastar::smp::count);
}

TokenBucket& RateLimitStore::get_or_create_bucket(
    const std::string& key,
    double capacity,
    double refill_rate_per_second)
{
    auto it = buckets_.find(key);
    if (it == buckets_.end()) {
        auto [inserted, success] = buckets_.emplace(
            key,
            TokenBucket(capacity, refill_rate_per_second)
            );
        return inserted->second;
    }
    return it->second;
}

BucketState RateLimitStore::bucket_to_state(const TokenBucket& bucket)
{
    // Note : bucket.available_raw() ne fait pas de refill.
    // Le caller doit avoir appelé refill() avant si nécessaire.
    BucketState state;
    state.remaining = bucket.available_raw();
    state.capacity = bucket.capacity();
    state.allowed = (state.remaining >= 1.0);
    state.retry_after_seconds = state.allowed
                                    ? 0
                                    : bucket.seconds_until_next_token();
    return state;
}

seastar::future<BucketState> RateLimitStore::consume(
    std::string key,
    double capacity,
    double refill_rate_per_second)
{
    auto& bucket = get_or_create_bucket(key, capacity, refill_rate_per_second);

    const bool allowed = bucket.try_consume();  // refill + decrement si possible

    BucketState state;
    state.allowed = allowed;
    state.remaining = bucket.available_raw();
    state.capacity = bucket.capacity();
    state.retry_after_seconds = allowed
                                    ? 0
                                    : bucket.seconds_until_next_token();

    return seastar::make_ready_future<BucketState>(state);
}

seastar::future<BucketState> RateLimitStore::peek(
    std::string key,
    double capacity,
    double refill_rate_per_second)
{
    auto it = buckets_.find(key);
    if (it == buckets_.end()) {
        // Bucket inexistant : on ne le crée pas (peek doit être pur en lecture).
        // On retourne l'état initial qu'aurait eu le bucket : full.
        BucketState state;
        state.allowed = (capacity >= 1.0);
        state.remaining = capacity;
        state.capacity = capacity;
        state.retry_after_seconds = 0;
        return seastar::make_ready_future<BucketState>(state);
    }

    auto& bucket = it->second;
    bucket.refill();   // met à jour les tokens selon le temps écoulé

    return seastar::make_ready_future<BucketState>(bucket_to_state(bucket));
}

seastar::future<> RateLimitStore::clear()
{
    buckets_.clear();
    return seastar::make_ready_future<>();
}

seastar::future<> RateLimitStore::stop()
{
    buckets_.clear();
    return seastar::make_ready_future<>();
}

} // namespace sea::http::middlewares