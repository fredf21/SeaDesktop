#include "token_tracking_config.h"

#include <stdexcept>
#include <utility>

namespace sea::domain::security {

// ===== Factory =====

TokenTrackingConfig TokenTrackingConfig::disabled()
{
    TokenTrackingConfig cfg;
    cfg.enabled_ = false;
    return cfg;
}

// ===== Setters =====

TokenTrackingConfig& TokenTrackingConfig::set_enabled(bool v)
{
    enabled_ = v;
    return *this;
}

TokenTrackingConfig& TokenTrackingConfig::set_refresh_table(std::string name)
{
    refresh_table_ = std::move(name);
    return *this;
}

TokenTrackingConfig& TokenTrackingConfig::set_revoked_table(std::string name)
{
    revoked_table_ = std::move(name);
    return *this;
}

TokenTrackingConfig& TokenTrackingConfig::set_cache(CacheConfig cfg)
{
    cache_ = std::move(cfg);
    return *this;
}

TokenTrackingConfig& TokenTrackingConfig::set_rotation(RotationConfig cfg)
{
    rotation_ = std::move(cfg);
    return *this;
}

TokenTrackingConfig& TokenTrackingConfig::set_auto_cleanup(AutoCleanupConfig cfg)
{
    auto_cleanup_ = std::move(cfg);
    return *this;
}

// ===== Validation =====

void TokenTrackingConfig::validate() const
{
    if (!enabled_) {
        // Rien à valider si désactivé
        return;
    }

    if (refresh_table_.empty()) {
        throw std::invalid_argument(
            "TokenTrackingConfig: refresh_table cannot be empty when enabled"
            );
    }
    if (revoked_table_.empty()) {
        throw std::invalid_argument(
            "TokenTrackingConfig: revoked_table cannot be empty when enabled"
            );
    }
    if (refresh_table_ == revoked_table_) {
        throw std::invalid_argument(
            "TokenTrackingConfig: refresh_table and revoked_table must be different "
            "(got both = '" + refresh_table_ + "')"
            );
    }

    if (cache_.enabled) {
        if (cache_.max_size == 0) {
            throw std::invalid_argument(
                "TokenTrackingConfig: cache.max_size must be > 0 when cache is enabled"
                );
        }
        if (cache_.ttl.count() <= 0) {
            throw std::invalid_argument(
                "TokenTrackingConfig: cache.ttl must be > 0 when cache is enabled"
                );
        }
    }

    if (auto_cleanup_.enabled) {
        if (auto_cleanup_.interval.count() <= 0) {
            throw std::invalid_argument(
                "TokenTrackingConfig: auto_cleanup.interval must be > 0 when enabled"
                );
        }
        if (auto_cleanup_.keep_revoked_for.count() < 0) {
            throw std::invalid_argument(
                "TokenTrackingConfig: auto_cleanup.keep_revoked_for cannot be negative"
                );
        }
    }
}

} // namespace sea::domain::security