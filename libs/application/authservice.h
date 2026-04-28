#ifndef AUTHSERVICE_H
#define AUTHSERVICE_H

#include "security_scheme/authentification_config.h"
#include "thread_pool_execution/i_blocking_executor.h"

#include <seastar/core/future.hh>

#include <chrono>
#include <optional>
#include <string>

namespace sea::application {

/**
 * Claims utilisateur extraits depuis un access token JWT valide.
 *
 * Ces informations peuvent ensuite être injectées dans la requête
 * par ton middleware d'authentification.
 */
struct AuthUserClaims {
    std::string user_id;
    std::string email;
    std::string role;
};

/**
 * AuthService
 *
 * Service applicatif responsable de :
 * - hasher les mots de passe
 * - vérifier les mots de passe
 * - générer les tokens JWT
 * - vérifier les tokens JWT
 *
 * Important avec Seastar :
 * Les opérations bcrypt et certaines opérations JWT utilisent libcrypto/OpenSSL.
 * Elles peuvent être coûteuses en CPU.
 *
 * Donc :
 * - les méthodes synchrones restent disponibles pour du code simple/test
 * - les méthodes async utilisent IBlockingExecutor pour éviter les stalls reactor
 *
 * Compatible Docker :
 * Le service ne dépend pas de chemins locaux.
 * Il reçoit toute sa configuration depuis AuthentificationConfig.
 */
class AuthService
{
public:
    /**
     * Constructeur principal.
     *
     * @param config Configuration d'authentification lue depuis le YAML.
     * @param service_name Nom du service utilisé comme issuer par défaut
     *        si jwt_issuer n'est pas fourni.
     */
    explicit AuthService(
        sea::domain::security::AuthentificationConfig config,
        std::string service_name = "seadesktop"
        );

    // ─────────────────────────────────────────────────────
    // API SYNCHRONE
    // ─────────────────────────────────────────────────────
    //
    // À éviter directement dans les handlers Seastar si l'opération peut être lourde.
    // Préférer les versions *_async avec IBlockingExecutor.

    /**
     * Hashe un mot de passe en clair avec BCrypt.
     *
     * Attention :
     * BCrypt est CPU-bound.
     * Ne pas appeler directement depuis un reactor Seastar en production.
     */
    [[nodiscard]]
    std::string hash_password(const std::string& plain_password) const;

    /**
     * Vérifie qu'un mot de passe en clair correspond au hash stocké.
     *
     * Attention :
     * BCrypt::validatePassword est CPU-bound.
     */
    [[nodiscard]]
    bool verify_password(
        const std::string& plain_password,
        const std::string& hashed_password
        ) const;

    /**
     * Génère un access token JWT.
     *
     * Utilisé pour accéder aux routes protégées.
     */
    [[nodiscard]]
    std::string generate_access_token(
        const std::string& user_id,
        const std::string& email,
        const std::string& role = "user"
        ) const;

    /**
     * Génère un refresh token JWT.
     *
     * Utilisé pour renouveler un access token.
     */
    [[nodiscard]]
    std::string generate_refresh_token(
        const std::string& user_id
        ) const;

    /**
     * Vérifie un access token JWT.
     *
     * Retourne les claims si le token est valide.
     */
    [[nodiscard]]
    std::optional<AuthUserClaims> verify_token(
        const std::string& token
        ) const;

    /**
     * Vérifie un refresh token JWT.
     *
     * Retourne le user_id si le token est valide.
     */
    [[nodiscard]]
    std::optional<std::string> verify_refresh_token(
        const std::string& token
        ) const;

    // ─────────────────────────────────────────────────────
    // API ASYNCHRONE SEASTAR
    // ─────────────────────────────────────────────────────
    //
    // Ces méthodes déplacent le travail CPU/bloquant dans ton thread pool.
    // Le reactor Seastar reste libre pour traiter les requêtes réseau.

    /**
     * Version async de hash_password.
     *
     * Le hash BCrypt est exécuté dans le blocking executor.
     */
    [[nodiscard]]
    seastar::future<std::string> hash_password_async(
        const std::string& plain_password,
        IBlockingExecutor& executor
        ) const;

    /**
     * Version async de verify_password.
     *
     * La vérification BCrypt est exécutée hors reactor.
     */
    [[nodiscard]]
    seastar::future<bool> verify_password_async(
        const std::string& plain_password,
        const std::string& hashed_password,
        IBlockingExecutor& executor
        ) const;

    /**
     * Version async de generate_access_token.
     *
     * Utile si la signature JWT passe par libcrypto/OpenSSL.
     */
    [[nodiscard]]
    seastar::future<std::string> generate_access_token_async(
        const std::string& user_id,
        const std::string& email,
        const std::string& role,
        IBlockingExecutor& executor
        ) const;

    /**
     * Version async de generate_refresh_token.
     */
    [[nodiscard]]
    seastar::future<std::string> generate_refresh_token_async(
        const std::string& user_id,
        IBlockingExecutor& executor
        ) const;

    /**
     * Version async de verify_token.
     *
     * À utiliser dans ProtectedHandler/AuthMiddleware si verify_token
     * apparaît dans les traces de stall.
     */
    [[nodiscard]]
    seastar::future<std::optional<AuthUserClaims>> verify_token_async(
        const std::string& token,
        IBlockingExecutor& executor
        ) const;

    /**
     * Version async de verify_refresh_token.
     */
    [[nodiscard]]
    seastar::future<std::optional<std::string>> verify_refresh_token_async(
        const std::string& token,
        IBlockingExecutor& executor
        ) const;

    // ─────────────────────────────────────────────────────
    // Accesseurs
    // ─────────────────────────────────────────────────────

    [[nodiscard]]
    const sea::domain::security::AuthentificationConfig&
    config() const noexcept
    {
        return config_;
    }

    [[nodiscard]]
    std::chrono::seconds access_token_ttl() const noexcept
    {
        return config_.access_token_ttl();
    }

    [[nodiscard]]
    std::chrono::seconds refresh_token_ttl() const noexcept
    {
        return config_.refresh_token_ttl();
    }

    [[nodiscard]]
    const std::string& issuer() const noexcept
    {
        return effective_issuer_;
    }

private:
    sea::domain::security::AuthentificationConfig config_;

    /**
     * Issuer effectif du JWT.
     *
     * Priorité :
     * 1. jwt_issuer depuis le YAML
     * 2. service_name fourni au constructeur
     */
    std::string effective_issuer_;
};

} // namespace sea::application

#endif // AUTHSERVICE_H