#include "token_tracking_service.h"

#include <runtime/dynamic_record.h>

#include <chrono>
#include <iomanip>
#include <sstream>
#include <utility>

#include <seastar/core/coroutine.hh>
#include <spdlog/spdlog.h>

namespace sea::application::auth {

namespace {

namespace persistence = sea::infrastructure::persistence;
namespace runtime     = sea::infrastructure::runtime;

// Helpers d'extraction de champs depuis un DynamicRecord
[[nodiscard]] std::optional<std::string>
get_string_field(const runtime::DynamicRecord& record, const std::string& field)
{
    const auto it = record.find(field);
    if (it == record.end()) return std::nullopt;
    if (std::holds_alternative<std::string>(it->second)) {
        return std::get<std::string>(it->second);
    }
    return std::nullopt;
}

// Parse "YYYY-MM-DD HH:MM:SS" en system_clock::time_point.
// Format MySQL DATETIME standard.
[[nodiscard]] std::optional<std::chrono::system_clock::time_point>
parse_datetime_string(const std::string& s)
{
    if (s.empty()) return std::nullopt;

    std::tm tm{};
    std::istringstream ss(s);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) return std::nullopt;

    // Note : std::mktime traite tm comme heure LOCALE. Pour l'UTC il
    // faudrait timegm() (POSIX). Pour le MVP on accepte la conversion
    // locale ; en prod, stocker en UTC partout.
    const auto tt = std::mktime(&tm);
    if (tt == -1) return std::nullopt;
    return std::chrono::system_clock::from_time_t(tt);
}

} // namespace anonyme


// ═════════════════════════════════════════════════════════════════════
// Construction
// ═════════════════════════════════════════════════════════════════════

TokenTrackingService::TokenTrackingService(
    std::shared_ptr<persistence::IGenericRepository> repository,
    sea::domain::security::TokenTrackingConfig config)
    : repository_(std::move(repository))
    , config_(std::move(config))
    , cache_(config_.cache().ttl, config_.cache().max_size)
{
    if (auto log = spdlog::get("sea.application")) {
        log->info(
            "TokenTrackingService initialized: enabled={} cache={} rotation={} "
            "refresh_table='{}' revoked_table='{}'",
            config_.is_enabled() ? "true" : "false",
            config_.cache().is_enabled() ? "true" : "false",
            config_.rotation().is_enabled() ? "true" : "false",
            config_.refresh_table(),
            config_.revoked_table());
    }
}

std::string TokenTrackingService::time_point_to_string(
    std::chrono::system_clock::time_point tp) const
{
    const auto tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

seastar::future<bool> TokenTrackingService::is_access_revoked(const std::string& jti)
{
    if (!config_.is_enabled()) {
        co_return false;   // tracking off -> on accepte tout
    }

    if (jti.empty()) {
        // Token sans jti : non trackable. On laisse passer (compat) — la
        // signature et l'exp restent verifies en amont par JwtService.
        co_return false;
    }

    // 1) Cache shard-local
    if (config_.cache().is_enabled()) {
        if (auto cached = cache_.lookup(jti); cached.has_value()) {
            co_return *cached;
        }
    }

    // 2) Lookup DB
    const auto record = co_await repository_->find_one_by_field(
        config_.revoked_table(), "jti", jti
        );

    bool revoked = false;
    if (record.has_value()) {
        // Verifie que l'entree n'est pas elle-meme expiree
        // (expires_at = quand le token expire naturellement)
        // Si l'entree de denylist est passee son expiration + keep_revoked_for,
        // on considere comme "non revoque" (le cleanup aurait du la purger).
        const auto expires_at_str = get_string_field(*record, "expires_at");
        if (expires_at_str.has_value()) {
            const auto expires_at = parse_datetime_string(*expires_at_str);
            if (expires_at.has_value() &&
                *expires_at >= std::chrono::system_clock::now()) {
                revoked = true;
            }
            // sinon : entree obsolete, on traite comme non revoque
        } else {
            // Pas de champ expires_at : on considere revoque par defaut (paranoia)
            revoked = true;
        }
    }

    // 3) Cache la decision
    if (config_.cache().is_enabled()) {
        cache_.put(jti, revoked);
    }

    co_return revoked;
}

seastar::future<> TokenTrackingService::revoke_access(
    const std::string& jti,
    const std::string& user_id,
    std::chrono::system_clock::time_point expires_at,
    const std::string& reason)
{
    if (!config_.is_enabled()) {
        co_return;
    }
    if (jti.empty()) {
        co_return;   // ne peut pas revoquer sans identifiant
    }

    // INSERT dans la denylist
    runtime::DynamicRecord record;
    record["jti"]         = jti;
    record["user_id"]     = user_id;
    record["revoked_at"]  = time_point_to_string(std::chrono::system_clock::now());
    record["expires_at"]  = time_point_to_string(expires_at);
    if (!reason.empty()) {
        record["reason"]  = reason;
    }

    co_await repository_->create(config_.revoked_table(), std::move(record));

    // Invalide le cache local. Les autres shards expireront leur cache
    // dans <= cache.ttl secondes (politique TTL court assumee).
    if (config_.cache().is_enabled()) {
        cache_.invalidate(jti);
    }

    if (auto log = spdlog::get("sea.application")) {
        log->info(
            "revoke_access: jti='{}' user_id='{}' reason='{}'",
            jti, user_id, reason);
    }

    co_return;
}


// ═════════════════════════════════════════════════════════════════════
// Refresh tokens — allowlist
// ═════════════════════════════════════════════════════════════════════

seastar::future<bool> TokenTrackingService::is_refresh_valid(const std::string& jti)
{
    if (!config_.is_enabled()) {
        co_return true;
    }
    if (jti.empty()) {
        co_return false;
    }

    const auto record = co_await repository_->find_one_by_field(
        config_.refresh_table(), "jti", jti
        );
    if (!record.has_value()) {
        co_return false;
    }

    // Verifie qu'il n'est pas revoque
    const auto revoked_at = get_string_field(*record, "revoked_at");
    if (revoked_at.has_value() && !revoked_at->empty()) {
        co_return false;
    }

    // Verifie qu'il n'est pas expire
    const auto expires_at_str = get_string_field(*record, "expires_at");
    if (expires_at_str.has_value()) {
        const auto expires_at = parse_datetime_string(*expires_at_str);
        if (expires_at.has_value() &&
            *expires_at < std::chrono::system_clock::now()) {
            co_return false;
        }
    }

    co_return true;
}

seastar::future<> TokenTrackingService::register_refresh(
    const std::string& jti,
    const std::string& user_id,
    std::chrono::system_clock::time_point issued_at,
    std::chrono::system_clock::time_point expires_at,
    const std::string& device_info,
    const std::string& ip_address)
{
    if (!config_.is_enabled()) {
        co_return;
    }
    if (jti.empty() || user_id.empty()) {
        co_return;   // donnees incompletes
    }

    runtime::DynamicRecord record;
    record["jti"]         = jti;
    record["user_id"]     = user_id;
    record["issued_at"]   = time_point_to_string(issued_at);
    record["expires_at"]  = time_point_to_string(expires_at);
    if (!device_info.empty()) record["device_info"] = device_info;
    if (!ip_address.empty())  record["ip_address"]  = ip_address;

    co_await repository_->create(config_.refresh_table(), std::move(record));

    if (auto log = spdlog::get("sea.application")) {
        log->info(
            "register_refresh: jti='{}' user_id='{}' ip='{}'",
            jti, user_id, ip_address);
    }

    co_return;
}

seastar::future<bool> TokenTrackingService::rotate_refresh(
    const std::string& old_jti,
    const std::string& new_jti,
    const std::string& user_id,
    std::chrono::system_clock::time_point new_issued_at,
    std::chrono::system_clock::time_point new_expires_at,
    const std::string& device_info,
    const std::string& ip_address)
{
    auto log = spdlog::get("sea.application");

    if (!config_.is_enabled()) {
        co_return false;
    }

    const bool rotate_old = config_.rotation().is_enabled();
    bool rotation_ok = true;
    // Cause d'echec pour le log final
    std::string failure_reason;

    // Idealement on fait ca en transaction. On utilise in_transaction
    // du repo qui passera en mode "memoire" pour InMemory (no-op) et
    // BEGIN/COMMIT pour MySQL.
    const auto tx_result = co_await repository_->in_transaction(
        [&]() -> seastar::future<bool> {
            if (rotate_old) {
                // 1) Recupere l'ancien refresh pour valider qu'il existe et est actif
                const auto old_record = co_await repository_->find_one_by_field(
                    config_.refresh_table(), "jti", old_jti
                    );
                if (!old_record.has_value()) {
                    rotation_ok = false;
                    failure_reason = "old_token_not_found";
                    co_return false;   // pas trouve -> rollback
                }

                const auto revoked_at = get_string_field(*old_record, "revoked_at");
                if (revoked_at.has_value() && !revoked_at->empty()) {
                    rotation_ok = false;
                    failure_reason = "old_token_already_revoked";
                    co_return false;   // deja revoque
                }

                // 2) UPDATE pour marquer revoked
                const auto id_field = get_string_field(*old_record, "id");
                if (!id_field.has_value()) {
                    rotation_ok = false;
                    failure_reason = "old_token_no_id";
                    co_return false;
                }

                runtime::DynamicRecord update_fields;
                update_fields["revoked_at"]      = time_point_to_string(
                    std::chrono::system_clock::now()
                    );
                update_fields["replaced_by_jti"] = new_jti;

                const auto update_result = co_await repository_->update(
                    config_.refresh_table(), *id_field, std::move(update_fields)
                    );
                if (!update_result.status) {
                    rotation_ok = false;
                    failure_reason = "update_old_failed";
                    co_return false;
                }
            }

            // 3) INSERT le nouveau refresh
            runtime::DynamicRecord new_record;
            new_record["jti"]         = new_jti;
            new_record["user_id"]     = user_id;
            new_record["issued_at"]   = time_point_to_string(new_issued_at);
            new_record["expires_at"]  = time_point_to_string(new_expires_at);
            if (!device_info.empty()) new_record["device_info"] = device_info;
            if (!ip_address.empty())  new_record["ip_address"]  = ip_address;

            const auto inserted = co_await repository_->create(
                config_.refresh_table(), std::move(new_record)
                );
            if (!inserted.has_value()) {
                rotation_ok = false;
                failure_reason = "insert_new_failed";
                co_return false;
            }

            co_return true;   // commit
        }
        );

    const bool success = tx_result.committed && rotation_ok;

    if (log) {
        if (success) {
            log->info(
                "rotate_refresh: user_id='{}' old_jti='{}' new_jti='{}' ip='{}'",
                user_id, old_jti, new_jti, ip_address);
        } else {
            log->warn(
                "rotate_refresh_failed: user_id='{}' old_jti='{}' reason='{}'",
                user_id, old_jti,
                failure_reason.empty() ? "tx_not_committed" : failure_reason);
        }
    }

    co_return success;
}

seastar::future<bool> TokenTrackingService::revoke_refresh(const std::string& jti)
{
    if (!config_.is_enabled()) {
        co_return false;
    }

    const auto record = co_await repository_->find_one_by_field(
        config_.refresh_table(), "jti", jti
        );
    if (!record.has_value()) {
        if (auto log = spdlog::get("sea.application")) {
            log->debug("revoke_refresh: jti='{}' not_found", jti);
        }
        co_return false;
    }

    const auto id_field = get_string_field(*record, "id");
    if (!id_field.has_value()) {
        co_return false;
    }

    runtime::DynamicRecord update_fields;
    update_fields["revoked_at"] = time_point_to_string(
        std::chrono::system_clock::now()
        );

    const auto result = co_await repository_->update(
        config_.refresh_table(), *id_field, std::move(update_fields)
        );

    if (result.status) {
        if (auto log = spdlog::get("sea.application")) {
            // user_id pour faciliter la traque
            const auto user_id_str = get_string_field(*record, "user_id");
            log->info(
                "revoke_refresh: jti='{}' user_id='{}'",
                jti, user_id_str.value_or(""));
        }
    }

    co_return result.status;
}

seastar::future<> TokenTrackingService::revoke_all_user_tokens(const std::string& user_id)
{
    if (!config_.is_enabled()) {
        co_return;
    }

    // Recupere tous les refresh du user (find_all + filter cote service)
    // MVP : pas optimal en perf, mais simple. Une evolution future
    // pourrait ajouter une methode find_all_by_field au repo.
    const auto all_refresh = co_await repository_->find_all(config_.refresh_table());

    std::size_t revoked_count = 0;
    for (const auto& record : all_refresh) {
        const auto user_id_field = get_string_field(record, "user_id");
        if (!user_id_field.has_value() || *user_id_field != user_id) {
            continue;
        }
        const auto revoked_at = get_string_field(record, "revoked_at");
        if (revoked_at.has_value() && !revoked_at->empty()) {
            continue;   // deja revoque, skip
        }
        const auto id_field = get_string_field(record, "id");
        if (!id_field.has_value()) {
            continue;
        }

        runtime::DynamicRecord update_fields;
        update_fields["revoked_at"] = time_point_to_string(
            std::chrono::system_clock::now()
            );
        co_await repository_->update(
            config_.refresh_table(), *id_field, std::move(update_fields)
            );
        ++revoked_count;
    }

    // Invalide tout le cache local (les access du user seront refuses
    // au prochain lookup DB qui ne les trouvera pas — sauf ceux deja
    // explicitement revoque dans revoked_table)
    if (config_.cache().is_enabled()) {
        cache_.clear();
    }

    // Event securite important : force-logout de toutes les sessions
    // d'un user. Niveau info, voire warn si frequent (admin force,
    // breach response, etc.).
    if (auto log = spdlog::get("sea.application")) {
        log->info(
            "revoke_all_user_tokens: user_id='{}' revoked_count={}",
            user_id, revoked_count);
    }

    co_return;
}


// ═════════════════════════════════════════════════════════════════════
// Audit
// ═════════════════════════════════════════════════════════════════════

seastar::future<std::vector<TokenTrackingService::ActiveSession>>
TokenTrackingService::list_active_sessions(const std::string& user_id)
{
    std::vector<ActiveSession> sessions;

    if (!config_.is_enabled()) {
        co_return sessions;
    }

    const auto all_refresh = co_await repository_->find_all(config_.refresh_table());
    const auto now = std::chrono::system_clock::now();

    for (const auto& record : all_refresh) {
        const auto user_id_field = get_string_field(record, "user_id");
        if (!user_id_field.has_value() || *user_id_field != user_id) continue;

        const auto revoked_at_str = get_string_field(record, "revoked_at");
        if (revoked_at_str.has_value() && !revoked_at_str->empty()) continue;

        const auto expires_at_str = get_string_field(record, "expires_at");
        if (!expires_at_str.has_value()) continue;
        const auto expires_at = parse_datetime_string(*expires_at_str);
        if (!expires_at.has_value() || *expires_at < now) continue;

        ActiveSession s;
        s.jti          = get_string_field(record, "jti").value_or("");
        s.user_id      = user_id;
        // issued_at : parse depuis le champ string en base
        if (const auto issued_at_str = get_string_field(record, "issued_at");
            issued_at_str.has_value()) {
            const auto issued = parse_datetime_string(*issued_at_str);
            if (issued.has_value()) {
                s.issued_at = *issued;
            }
        }
        // expires_at : deja parse plus haut, on le reutilise
        s.expires_at   = *expires_at;
        s.device_info  = get_string_field(record, "device_info").value_or("");
        s.ip_address   = get_string_field(record, "ip_address").value_or("");
        sessions.push_back(std::move(s));
    }

    co_return sessions;
}

seastar::future<TokenTrackingService::CleanupReport>
TokenTrackingService::cleanup_expired()
{
    CleanupReport report;
    if (!config_.is_enabled()) {
        co_return report;
    }

    const auto now = std::chrono::system_clock::now();

    // 1) Refresh expires
    const auto all_refresh = co_await repository_->find_all(config_.refresh_table());
    for (const auto& record : all_refresh) {
        const auto expires_at_str = get_string_field(record, "expires_at");
        if (!expires_at_str.has_value()) continue;
        const auto expires_at = parse_datetime_string(*expires_at_str);
        if (!expires_at.has_value()) continue;
        if (*expires_at >= now) continue;

        const auto id_field = get_string_field(record, "id");
        if (!id_field.has_value()) continue;
        const bool deleted = co_await repository_->remove(
            config_.refresh_table(), *id_field
            );
        if (deleted) ++report.refresh_deleted;
    }

    // 2) Denylist obsoletes
    const auto keep_for = config_.auto_cleanup().keep_revoked_for;
    const auto cutoff = now - keep_for;

    const auto all_revoked = co_await repository_->find_all(config_.revoked_table());
    for (const auto& record : all_revoked) {
        const auto expires_at_str = get_string_field(record, "expires_at");
        if (!expires_at_str.has_value()) continue;
        const auto expires_at = parse_datetime_string(*expires_at_str);
        if (!expires_at.has_value()) continue;
        // On garde l'entree tant que (expires_at + keep_revoked_for) > now
        // soit expires_at > now - keep_revoked_for = cutoff
        if (*expires_at >= cutoff) continue;

        const auto id_field = get_string_field(record, "id");
        if (!id_field.has_value()) continue;
        const bool deleted = co_await repository_->remove(
            config_.revoked_table(), *id_field
            );
        if (deleted) ++report.revoked_deleted;
    }

    if (auto log = spdlog::get("sea.application")) {
        log->info(
            "cleanup_expired: refresh_deleted={} revoked_deleted={}",
            report.refresh_deleted, report.revoked_deleted);
    }

    co_return report;
}

} // namespace sea::application::auth