#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>

namespace sea::application::auth {

// ─────────────────────────────────────────────────────────────────────
// DenylistCache
//
// Cache shard-local des decisions de denylist pour les access tokens.
//
// Principes :
// - Une instance par shard Seastar (pas de partage entre shards)
// - Aucune synchronisation (cohérent avec shared-nothing Seastar)
// - Pour chaque jti consulte, on cache la decision (revoked / not revoked)
//   pendant un TTL configurable
// - Apres expiration TTL, le prochain lookup ira en DB (lazy refresh)
//
// Eviction :
// - LRU approximatif via TTL : les entrees expirees sont supprimees
//   au prochain acces qui les rencontre
// - Cap dur sur max_size : si plein, on purge les entrees expirees ;
//   si toujours plein, on n'ajoute pas (politique "first-in-stays")
//   pour eviter de devenir un vecteur de DoS memoire
//
// Cycle de vie d'une entree :
//   T+0    : lookup(jti) -> cache miss -> service va en DB -> put(jti, revoked)
//   T+0..T+TTL : lookup(jti) -> cache hit, decision retournee instantanement
//   T+TTL  : lookup(jti) -> cache miss (expiree) -> nouveau lookup DB
//
// Important : la decision cachee peut etre "non revoque" aussi. C'est
// le seul moyen d'eviter un lookup DB par requete pour les tokens valides.
// ─────────────────────────────────────────────────────────────────────
class DenylistCache {
public:
    /**
     * Cree un cache avec TTL et taille max.
     *
     * @param ttl       Duree de vie d'une entree (typiquement 60s)
     * @param max_size  Nombre max d'entrees (typiquement 10000)
     */
    DenylistCache(std::chrono::seconds ttl, std::size_t max_size);

    /**
     * Consulte le cache pour un jti donne.
     *
     * @return  std::nullopt : pas en cache, ou entree expiree
     *          true         : jti revoque (cache hit, refuser le token)
     *          false        : jti pas revoque (cache hit, accepter le token)
     */
    [[nodiscard]] std::optional<bool> lookup(const std::string& jti);

    /**
     * Met en cache la decision pour un jti.
     *
     * @param jti      JWT ID
     * @param revoked  true si revoque, false si pas revoque
     */
    void put(const std::string& jti, bool revoked);

    /**
     * Invalide explicitement une entree (utilise par TokenTrackingService
     * apres une revocation effectuee sur ce shard).
     *
     * Si le cache d'autres shards detient encore l'entree, ils continuent
     * a l'utiliser jusqu'a leur propre TTL. C'est le compromis assume
     * de la strategie "TTL court sans propagation cross-shard".
     */
    void invalidate(const std::string& jti);

    /**
     * Vide tout le cache (utilise par tests ou cleanup admin).
     */
    void clear();

    /**
     * Nombre d'entrees actuellement en cache (incluant les expirees
     * pas encore purgees).
     */
    [[nodiscard]] std::size_t size() const noexcept;

    /**
     * Statistiques (utile pour debug / endpoint /admin/logs futur).
     */
    struct Stats {
        std::size_t hits           = 0;
        std::size_t misses         = 0;
        std::size_t evictions      = 0;     // entrees supprimees par cap max_size
        std::size_t expired_purges = 0;     // entrees supprimees par TTL
    };

    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }
    void reset_stats() noexcept { stats_ = {}; }

private:
    struct Entry {
        bool                                  revoked;
        std::chrono::steady_clock::time_point inserted_at;
    };

    /**
     * Si le cache a atteint max_size_, supprime les entrees expirees.
     * Si toujours plein apres ca, ne rien ajouter (politique stricte).
     */
    void evict_expired_if_needed();

    [[nodiscard]] bool is_expired(const Entry& entry) const noexcept;

    const std::chrono::seconds ttl_;
    const std::size_t          max_size_;
    std::unordered_map<std::string, Entry> entries_;
    mutable Stats              stats_;
};

} // namespace sea::application::auth
