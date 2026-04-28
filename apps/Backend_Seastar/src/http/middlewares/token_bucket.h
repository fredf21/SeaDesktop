#ifndef TOKEN_BUCKET_H
#define TOKEN_BUCKET_H

#include <chrono>
#include <cstdint>

namespace sea::http::middlewares {

// Token bucket simple, non thread-safe.
// Sur Seastar, chaque shard a sa propre instance, donc pas de souci.
class TokenBucket {
public:
    TokenBucket(double capacity, double refill_rate_per_second);

    // Tente de consommer 1 token. Retourne true si OK, false si vide.
    // Refill automatique avant la tentative.
    bool try_consume();

    // Force un refill du bucket selon le temps écoulé,
    // sans rien consommer.
    void refill();

    // Retourne le nombre de tokens disponibles APRÈS refill.
    // Modifie l'état interne (refill) mais ne consomme pas.
    double available_now();

    // Retourne le nombre de tokens disponibles SANS refill.
    // Const, ne modifie pas l'état.
    double available_raw() const;

    // Capacité maximale du bucket
    double capacity() const { return capacity_; }

    // Refill rate (tokens/seconde)
    double refill_rate() const { return refill_rate_; }

    // Secondes avant le prochain token disponible (utile pour Retry-After).
    // Suppose un état à jour : appelle refill() avant si nécessaire.
    std::int64_t seconds_until_next_token() const;

private:
    void refill_now();

    double capacity_;
    double refill_rate_;
    double tokens_;
    std::chrono::steady_clock::time_point last_refill_;
};

} // namespace sea::http::middlewares

#endif // TOKEN_BUCKET_H