#ifndef RATE_LIMIT_STORE_H
#define RATE_LIMIT_STORE_H

#include "token_bucket.h"

#include <seastar/core/sharded.hh>
#include <seastar/core/future.hh>

#include <string>
#include <unordered_map>

namespace sea::http::middlewares {

// Résultat retourné par consume() ou peek()
struct BucketState {
    bool allowed;                       // tokens disponibles ?
    double remaining;                   // tokens restants (avant ou après consommation)
    double capacity;                    // capacité max du bucket
    std::int64_t retry_after_seconds;   // secondes à attendre si vide (0 sinon)
};

// Service sharded : chaque shard détient un sous-ensemble des buckets.
// Le sharding est fait par hash(key) % smp_count.
class RateLimitStore : public seastar::peering_sharded_service<RateLimitStore> {
public:
    RateLimitStore() = default;

    // Détermine quel shard détient une clé donnée.
    // Garantit qu'une même clé tombe toujours sur le même shard.
    static unsigned shard_of_key(const std::string& key);

    // Consomme un token et retourne l'état après consommation.
    // À appeler uniquement sur le shard owner.
    // Crée le bucket s'il n'existe pas encore.
    seastar::future<BucketState> consume(
        std::string key,
        double capacity,
        double refill_rate_per_second
        );

    // Lit l'état du bucket SANS consommer de token.
    // À appeler uniquement sur le shard owner.
    // Si le bucket n'existe pas, retourne un état "full" (capacity tokens).
    seastar::future<BucketState> peek(
        std::string key,
        double capacity,
        double refill_rate_per_second
        );

    // Vide tous les buckets (utile pour les tests / cleanup).
    seastar::future<> clear();

    // Méthode obligatoire de seastar::sharded<>
    seastar::future<> stop();

private:
    // Helper interne : récupère ou crée un bucket
    TokenBucket& get_or_create_bucket(
        const std::string& key,
        double capacity,
        double refill_rate_per_second);

    // Helper interne : convertit un bucket en BucketState (sans consommer)
    static BucketState bucket_to_state(const TokenBucket& bucket);

    std::unordered_map<std::string, TokenBucket> buckets_;
};

} // namespace sea::http::middlewares

#endif // RATE_LIMIT_STORE_H