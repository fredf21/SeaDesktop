#pragma once

#include "denylist_cache.h"
#include "security_scheme/token_tracking_config.h"

#include <persistence/i_generic_repository.h>

#include <chrono>
#include <memory>
#include <seastar/core/future.hh>
#include <string>

namespace sea::application::auth {

// ─────────────────────────────────────────────────────────────────────
// TokenTrackingService
//
// Service applicatif qui orchestre le tracking des tokens JWT selon
// l'architecture "Auth0-like" :
//
//   - Access tokens : stateless + denylist + cache shard-local
//       - is_access_revoked(jti) consulte le cache, fallback DB
//       - revoke_access(jti) ajoute en DB et invalide le cache local
//
//   - Refresh tokens : allowlist stricte
//       - is_refresh_valid(jti) : lookup DB obligatoire
//       - register_refresh(jti, user_id, ...) : ajoute a la table
//       - rotate_refresh(old_jti, new_jti, ...) : marque l'ancien
//         comme remplace, insere le nouveau
//       - revoke_refresh(jti) : marque revoked_at = now
//
//   - Logout / revocation globale :
//       - revoke_all_user_tokens(user_id) : revoke tous les refresh
//         du user
//
//   - Maintenance :
//       - cleanup_expired() : supprime les entrees expirees
//
// Le service depend uniquement de IGenericRepository (couche infra) et
// de la config du domaine. Aucune connaissance HTTP ou JWT cryptographic
// — c'est de la pure logique metier.
//
// Une instance par shard (cohérent avec shared-nothing Seastar).
// Le cache est shard-local, pas de synchronisation cross-shard.
// ─────────────────────────────────────────────────────────────────────
class TokenTrackingService {
public:
    /**
     * Construit un TokenTrackingService.
     *
     * @param repository      Le repo generique (acces DB)
     * @param config          Config du domaine (depuis le YAML)
     *
     * Si config.is_enabled() == false, toutes les operations deviennent
     * des no-op qui acceptent tout. Cela permet d'utiliser le service
     * uniformement meme quand le tracking est desactive.
     */
    TokenTrackingService(
        std::shared_ptr<sea::infrastructure::persistence::IGenericRepository> repository,
        sea::domain::security::TokenTrackingConfig config
        );

    // ═════════════════════════════════════════════════════════════════
    // Access tokens — denylist + cache
    // ═════════════════════════════════════════════════════════════════

    /**
     * Verifie si un access token a ete revoque.
     *
     * Algorithme :
     *   1. Consulte le cache shard-local
     *   2. Si cache hit : retourne la decision cachee
     *   3. Si cache miss : lookup en DB sur la table denylist
     *      - Trouve & non expire : revoke. Cache (true).
     *      - Pas trouve, ou expire : non revoke. Cache (false).
     *
     * Si tracking desactive : retourne toujours false (= accepter).
     *
     * @param jti  JWT ID du token a verifier
     * @return  true si revoque (refuser), false sinon (accepter)
     */
    seastar::future<bool> is_access_revoked(const std::string& jti);

    /**
     * Revoke un access token (logout, ban, compromission).
     *
     * Action :
     *   1. INSERT dans la table denylist (jti, user_id, revoked_at, expires_at, reason)
     *   2. Invalide l'entree dans le cache local
     *      (les autres shards expireront leur cache dans <= cache.ttl secondes)
     *
     * Si tracking desactive : no-op.
     *
     * @param jti          JWT ID
     * @param user_id      Pour audit
     * @param expires_at   Quand le token expire naturellement (pour
     *                     l'auto-cleanup de la denylist)
     * @param reason       Raison facultative ("logout", "banned", "compromised")
     */
    seastar::future<> revoke_access(
        const std::string& jti,
        const std::string& user_id,
        std::chrono::system_clock::time_point expires_at,
        const std::string& reason = "logout"
        );


    // ═════════════════════════════════════════════════════════════════
    // Refresh tokens — allowlist stricte
    // ═════════════════════════════════════════════════════════════════

    /**
     * Verifie si un refresh token est valide (present et non revoque).
     *
     * Algorithme : SELECT sur la table allowlist par jti :
     *   - Pas trouve : false (jamais emis, ou cleanup a passe)
     *   - revoked_at != null : false (revoque)
     *   - expires_at < now : false (expire)
     *   - sinon : true
     *
     * Si tracking desactive : retourne toujours true (= accepter).
     */
    seastar::future<bool> is_refresh_valid(const std::string& jti);

    /**
     * Enregistre un nouveau refresh token dans l'allowlist.
     * Appele apres login ou register.
     *
     * Si tracking desactive : no-op.
     */
    seastar::future<> register_refresh(
        const std::string& jti,
        const std::string& user_id,
        std::chrono::system_clock::time_point issued_at,
        std::chrono::system_clock::time_point expires_at,
        const std::string& device_info = "",
        const std::string& ip_address = ""
        );

    /**
     * Effectue une rotation de refresh token.
     *
     * Action atomique (transaction si possible) :
     *   1. UPDATE allowlist SET revoked_at = now(), replaced_by_jti = new_jti WHERE jti = old_jti
     *   2. INSERT le nouveau refresh dans l'allowlist
     *
     * Si rotation.is_enabled() == false : on insert juste le nouveau,
     * sans toucher a l'ancien.
     *
     * @return true si rotation effectuee, false si l'ancien jti n'est pas valide
     */
    seastar::future<bool> rotate_refresh(
        const std::string& old_jti,
        const std::string& new_jti,
        const std::string& user_id,
        std::chrono::system_clock::time_point new_issued_at,
        std::chrono::system_clock::time_point new_expires_at,
        const std::string& device_info = "",
        const std::string& ip_address = ""
        );

    /**
     * Revoke un refresh token specifique (logout, ban).
     *
     * @return true si trouve et revoque, false si pas trouve
     */
    seastar::future<bool> revoke_refresh(const std::string& jti);

    /**
     * Revoke tous les tokens (refresh ET cache d'access) d'un user.
     *
     * Action :
     *   1. UPDATE allowlist SET revoked_at = now() WHERE user_id = ? AND revoked_at IS NULL
     *   2. Invalide tout le cache local (les access seront alors refuses
     *      via la denylist DB qu'on ne peut pas remplir efficacement
     *      sans avoir la liste des jti — voir limitation ci-dessous)
     *
     * LIMITATION : on ne peut pas pre-remplir la denylist pour tous les
     * access tokens d'un user, car le serveur ne connait pas leur jti
     * sans les avoir vus passer. Resultat : les access tokens emis
     * recemment resteront valides jusqu'a leur exp naturelle.
     *
     * Mitigation : tu peux configurer access_token_ttl court (15min)
     * pour limiter la fenetre.
     *
     * Alternative future : maintenir aussi une table d'access tokens
     * actifs (Approche beta de notre cadrage), mais on a explicitement
     * choisi Option 3 (Auth0-like) pour la perf.
     */
    seastar::future<> revoke_all_user_tokens(const std::string& user_id);


    // ═════════════════════════════════════════════════════════════════
    // Audit / sessions actives (Cas B)
    // ═════════════════════════════════════════════════════════════════

    struct ActiveSession {
        std::string jti;
        std::string user_id;
        std::chrono::system_clock::time_point issued_at;
        std::chrono::system_clock::time_point expires_at;
        std::string device_info;
        std::string ip_address;
    };

    /**
     * Liste les sessions actives d'un user (refresh tokens non revoques
     * et non expires).
     */
    seastar::future<std::vector<ActiveSession>>
    list_active_sessions(const std::string& user_id);


    // ═════════════════════════════════════════════════════════════════
    // Maintenance (Cas A.4 — cleanup periodique)
    // ═════════════════════════════════════════════════════════════════

    struct CleanupReport {
        std::size_t refresh_deleted = 0;
        std::size_t revoked_deleted = 0;
    };

    /**
     * Supprime les entrees expirees :
     *   - allowlist : refresh dont expires_at < now()
     *   - denylist  : entrees dont (expires_at + keep_revoked_for) < now()
     *
     * Appele typiquement par un timer Seastar (cf config.auto_cleanup).
     */
    seastar::future<CleanupReport> cleanup_expired();


    // ═════════════════════════════════════════════════════════════════
    // Accesseurs
    // ═════════════════════════════════════════════════════════════════

    [[nodiscard]] const sea::domain::security::TokenTrackingConfig& config() const noexcept {
        return config_;
    }

    [[nodiscard]] DenylistCache::Stats cache_stats() const noexcept {
        return cache_.stats();
    }

private:
    std::shared_ptr<sea::infrastructure::persistence::IGenericRepository> repository_;
    sea::domain::security::TokenTrackingConfig                            config_;
    DenylistCache                                                         cache_;

    // Helpers
    [[nodiscard]] std::string time_point_to_string(
        std::chrono::system_clock::time_point tp) const;
};

} // namespace sea::application::auth
