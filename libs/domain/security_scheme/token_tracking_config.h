#pragma once

#include <chrono>
#include <cstddef>
#include <string>

namespace sea::domain::security {

/**
 * @brief Configuration du tracking des tokens JWT.
 *
 * Architecture (Option 3 - comme Google/Auth0) :
 *
 * - Access tokens : stateless + denylist + cache shard-local
 *     Vérification fast-path : signature + exp + cache lookup
 *     Révocation : insertion dans la table denylist, propagation via TTL du cache
 *
 * - Refresh tokens : allowlist stricte
 *     Chaque refresh = lookup DB obligatoire (rare donc OK perf)
 *     Rotation optionnelle : ancien marqué replaced_by_jti, nouveau inséré
 *     Logout = revoked_at = now() sur tous les refresh du user
 *
 * Configuration YAML :
 *
 *   token_tracking:
 *     enabled: true
 *     refresh_table: RefreshToken
 *     revoked_table: RevokedToken
 *     cache:
 *       enabled: true
 *       ttl: "5m"
 *       max_size: 10000
 *     rotation:
 *       enabled: true
 *     auto_cleanup:
 *       enabled: true
 *       interval: "1h"
 *       keep_revoked_for: "30d"
 */
class TokenTrackingConfig {
public:
    // ─── Sub-config : cache shard-local pour la denylist ─────────
    struct CacheConfig {
        bool        enabled  = true;
        std::chrono::seconds ttl       = std::chrono::minutes(5);
        std::size_t max_size            = 10'000;   // par shard

        [[nodiscard]] bool is_enabled() const noexcept { return enabled; }
    };

    // ─── Sub-config : rotation des refresh tokens ────────────────
    struct RotationConfig {
        bool enabled = true;   // à chaque refresh, l'ancien est révoqué

        [[nodiscard]] bool is_enabled() const noexcept { return enabled; }
    };

    // ─── Sub-config : nettoyage périodique ───────────────────────
    struct AutoCleanupConfig {
        bool enabled = true;
        std::chrono::seconds interval         = std::chrono::hours(1);
        std::chrono::seconds keep_revoked_for = std::chrono::hours(24 * 30);

        [[nodiscard]] bool is_enabled() const noexcept { return enabled; }
    };

    // ─── Constructeurs / Factory ─────────────────────────────────
    TokenTrackingConfig() = default;

    /// Désactivé (defaut hérité, comportement stateless pur)
    [[nodiscard]] static TokenTrackingConfig disabled();

    // ─── Builder fluide ──────────────────────────────────────────
    TokenTrackingConfig& set_enabled(bool v);
    TokenTrackingConfig& set_refresh_table(std::string name);
    TokenTrackingConfig& set_revoked_table(std::string name);
    TokenTrackingConfig& set_cache(CacheConfig cfg);
    TokenTrackingConfig& set_rotation(RotationConfig cfg);
    TokenTrackingConfig& set_auto_cleanup(AutoCleanupConfig cfg);

    // ─── Accesseurs ──────────────────────────────────────────────
    [[nodiscard]] bool                     is_enabled()      const noexcept { return enabled_; }
    [[nodiscard]] const std::string&       refresh_table()   const noexcept { return refresh_table_; }
    [[nodiscard]] const std::string&       revoked_table()   const noexcept { return revoked_table_; }
    [[nodiscard]] const CacheConfig&       cache()           const noexcept { return cache_; }
    [[nodiscard]] const RotationConfig&    rotation()        const noexcept { return rotation_; }
    [[nodiscard]] const AutoCleanupConfig& auto_cleanup()    const noexcept { return auto_cleanup_; }

    // ─── Validation ──────────────────────────────────────────────
    /**
     * @throws std::invalid_argument si la configuration est incohérente.
     * Conditions vérifiées (uniquement si enabled = true) :
     * - refresh_table et revoked_table non vides
     * - refresh_table != revoked_table
     * - cache.max_size > 0 si cache.enabled
     * - cache.ttl > 0 si cache.enabled
     * - auto_cleanup.interval > 0 si auto_cleanup.enabled
     * - auto_cleanup.keep_revoked_for >= 0
     */
    void validate() const;

private:
    bool              enabled_        = false;
    std::string       refresh_table_  = "RefreshToken";
    std::string       revoked_table_  = "RevokedToken";
    CacheConfig       cache_{};
    RotationConfig    rotation_{};
    AutoCleanupConfig auto_cleanup_{};
};

} // namespace sea::domain::security
