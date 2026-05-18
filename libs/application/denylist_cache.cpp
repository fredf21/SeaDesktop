#include "denylist_cache.h"


namespace sea::application::auth {

DenylistCache::DenylistCache(std::chrono::seconds ttl, std::size_t max_size)
    : ttl_(ttl)
    , max_size_(max_size)
{
}

bool DenylistCache::is_expired(const Entry& entry) const noexcept
{
    const auto now = std::chrono::steady_clock::now();
    return (now - entry.inserted_at) >= ttl_;
}

std::optional<bool> DenylistCache::lookup(const std::string& jti)
{
    const auto it = entries_.find(jti);
    if (it == entries_.end()) {
        ++stats_.misses;
        return std::nullopt;
    }

    if (is_expired(it->second)) {
        // Entree expiree : on la purge et on signale "miss"
        entries_.erase(it);
        ++stats_.misses;
        ++stats_.expired_purges;
        return std::nullopt;
    }

    ++stats_.hits;
    return it->second.revoked;
}

void DenylistCache::put(const std::string& jti, bool revoked)
{
    // Cap dur : si on est plein, on essaie de faire de la place
    if (entries_.size() >= max_size_) {
        evict_expired_if_needed();

        // Si meme apres purge on est plein, on n'ajoute pas
        // (politique anti-DoS : evite que la table croisse sans limite
        //  meme sous attaque)
        if (entries_.size() >= max_size_) {
            // On compte l'eviction "implicite" (on aurait pu, mais on choisit de ne pas)
            ++stats_.evictions;
            return;
        }
    }

    entries_[jti] = Entry{
        .revoked     = revoked,
        .inserted_at = std::chrono::steady_clock::now()
    };
}

void DenylistCache::invalidate(const std::string& jti)
{
    entries_.erase(jti);
}

void DenylistCache::clear()
{
    entries_.clear();
}

std::size_t DenylistCache::size() const noexcept
{
    return entries_.size();
}

void DenylistCache::evict_expired_if_needed()
{
    // Parcourt et supprime les entrees expirees.
    // Pas optimise pour les tres grands max_size (O(n)), mais
    // appele uniquement quand on atteint la limite (rare).
    for (auto it = entries_.begin(); it != entries_.end(); ) {
        if (is_expired(it->second)) {
            it = entries_.erase(it);
            ++stats_.expired_purges;
        } else {
            ++it;
        }
    }
}

} // namespace sea::application::auth