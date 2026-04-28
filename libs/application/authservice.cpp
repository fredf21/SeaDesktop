#include "authservice.h"

#include "security/jwt_service.h"

#include <bcrypt/BCrypt.hpp>

#include <stdexcept>
#include <utility>

namespace sea::application {

namespace {

/**
 * Construit les paramètres nécessaires pour générer un token JWT.
 *
 * Cette fonction ne signe pas le token.
 * Elle prépare seulement les données pour JwtService.
 */
sea::infrastructure::security::GenerateTokenParams make_generate_params(
    const sea::domain::security::AuthentificationConfig& cfg,
    const std::string& issuer,
    const std::string& user_id,
    const std::string& email,
    const std::string& role,
    sea::infrastructure::security::TokenType type,
    std::chrono::seconds ttl)
{
    return sea::infrastructure::security::GenerateTokenParams{
        .user_id    = user_id,
        .email      = email,
        .role       = role,
        .secret     = cfg.jwt_secret(),
        .issuer     = issuer,
        .token_type = type,
        .ttl        = ttl
    };
}

/**
 * Construit les paramètres nécessaires pour vérifier un token JWT.
 *
 * Cette fonction ne vérifie pas le token.
 * Elle prépare seulement les données pour JwtService.
 */
sea::infrastructure::security::VerifyTokenParams make_verify_params(
    const sea::domain::security::AuthentificationConfig& cfg,
    const std::string& issuer,
    const std::string& token,
    sea::infrastructure::security::TokenType expected_type)
{
    return sea::infrastructure::security::VerifyTokenParams{
        .token           = token,
        .secret          = cfg.jwt_secret(),
        .expected_issuer = issuer,
        .expected_type   = expected_type
    };
}

} // namespace

// ─────────────────────────────────────────────────────
// Constructeur
// ─────────────────────────────────────────────────────

AuthService::AuthService(
    sea::domain::security::AuthentificationConfig config,
    std::string service_name)
    : config_(std::move(config))
{
    using AuthType = sea::domain::security::AuthType;

    /**
     * Valide la configuration de sécurité.
     *
     * Exemple :
     * - type d'auth valide
     * - TTL cohérents
     * - secret JWT présent ou généré avant
     */
    config_.validate();

    /**
     * Pour le moment, AuthService supporte uniquement JWT.
     *
     * Si tu ajoutes plus tard OAuth2/API Key/Session Cookie,
     * c'est ici que tu pourras brancher les autres stratégies.
     */
    if (config_.type() != AuthType::Jwt) {
        throw std::runtime_error(
            "AuthService: seul le type JWT est supporte pour le moment "
            "(type recu: " + std::string(to_string(config_.type())) + ")"
            );
    }

    /**
     * Détermine l'issuer du JWT.
     *
     * En Docker, il est préférable de fournir jwt_issuer dans le YAML
     * ou via une configuration générée depuis les variables d'environnement.
     *
     * Si jwt_issuer est absent, on utilise service_name.
     */
    if (!config_.jwt_issuer().empty()) {
        effective_issuer_ = config_.jwt_issuer();
    } else {
        if (service_name.empty()) {
            throw std::runtime_error(
                "AuthService: ni jwt_issuer ni service_name fourni"
                );
        }

        effective_issuer_ = std::move(service_name);
    }
}

// ─────────────────────────────────────────────────────
// Mots de passe - versions synchrones
// ─────────────────────────────────────────────────────

std::string AuthService::hash_password(
    const std::string& plain_password) const
{
    /**
     * BCrypt est volontairement coûteux.
     *
     * Ne pas appeler cette méthode directement depuis un handler Seastar.
     * Utiliser hash_password_async(...) dans le flow HTTP.
     */
    return BCrypt::generateHash(plain_password);
}

bool AuthService::verify_password(
    const std::string& plain_password,
    const std::string& hashed_password) const
{
    /**
     * Vérifie un password contre un hash BCrypt.
     *
     * Cette opération peut prendre plusieurs millisecondes.
     * Utiliser verify_password_async(...) dans le flow HTTP.
     */
    return BCrypt::validatePassword(plain_password, hashed_password);
}

// ─────────────────────────────────────────────────────
// Tokens - versions synchrones
// ─────────────────────────────────────────────────────

std::string AuthService::generate_access_token(
    const std::string& user_id,
    const std::string& email,
    const std::string& role) const
{
    using namespace sea::infrastructure::security;

    const auto params = make_generate_params(
        config_,
        effective_issuer_,
        user_id,
        email,
        role,
        TokenType::Access,
        config_.access_token_ttl()
        );

    /**
     * Génère un JWT signé.
     *
     * Selon l'algorithme, la signature peut passer par libcrypto/OpenSSL.
     */
    return JwtService::generate_token(params);
}

std::string AuthService::generate_refresh_token(
    const std::string& user_id) const
{
    using namespace sea::infrastructure::security;

    const auto params = make_generate_params(
        config_,
        effective_issuer_,
        user_id,
        "",                      // pas d'email dans un refresh token
        "",                      // pas de role dans un refresh token
        TokenType::Refresh,
        config_.refresh_token_ttl()
        );

    return JwtService::generate_token(params);
}

std::optional<AuthUserClaims> AuthService::verify_token(
    const std::string& token) const
{
    using namespace sea::infrastructure::security;

    const auto params = make_verify_params(
        config_,
        effective_issuer_,
        token,
        TokenType::Access
        );

    const auto claims = JwtService::verify_token(params);
    if (!claims.has_value()) {
        return std::nullopt;
    }

    AuthUserClaims result;
    result.user_id = claims->user_id;
    result.email   = claims->email;
    result.role    = claims->role;

    return result;
}

std::optional<std::string> AuthService::verify_refresh_token(
    const std::string& token) const
{
    using namespace sea::infrastructure::security;

    const auto params = make_verify_params(
        config_,
        effective_issuer_,
        token,
        TokenType::Refresh
        );

    const auto claims = JwtService::verify_token(params);
    if (!claims.has_value()) {
        return std::nullopt;
    }

    return claims->user_id;
}

// ─────────────────────────────────────────────────────
// Mots de passe - versions async Seastar
// ─────────────────────────────────────────────────────

seastar::future<std::string>
AuthService::hash_password_async(
    const std::string& plain_password,
    IBlockingExecutor& executor) const
{
    /**
     * Exécute BCrypt dans le thread pool.
     *
     * Le reactor Seastar reste disponible pour :
     * - accepter de nouvelles connexions
     * - lire/écrire les sockets
     * - continuer les autres futures
     */
    return executor.submit(
        [this, plain_password] {
            return hash_password(plain_password);
        }
        );
}

seastar::future<bool>
AuthService::verify_password_async(
    const std::string& plain_password,
    const std::string& hashed_password,
    IBlockingExecutor& executor) const
{
    /**
     * Exécute la vérification BCrypt dans le thread pool.
     *
     * C'est la méthode à utiliser dans LoginHandler.
     */
    return executor.submit(
        [this, plain_password, hashed_password] {
            return verify_password(plain_password, hashed_password);
        }
        );
}

// ─────────────────────────────────────────────────────
// Tokens - versions async Seastar
// ─────────────────────────────────────────────────────

seastar::future<std::string>
AuthService::generate_access_token_async(
    const std::string& user_id,
    const std::string& email,
    const std::string& role,
    IBlockingExecutor& executor) const
{
    /**
     * Génère le token hors reactor.
     *
     * Même si HS256 est souvent rapide, ton log montre libcrypto dans
     * les stalls. On sort donc aussi la signature JWT du reactor.
     */
    return executor.submit(
        [this, user_id, email, role] {
            return generate_access_token(user_id, email, role);
        }
        );
}

seastar::future<std::string>
AuthService::generate_refresh_token_async(
    const std::string& user_id,
    IBlockingExecutor& executor) const
{
    /**
     * Génère le refresh token hors reactor.
     */
    return executor.submit(
        [this, user_id] {
            return generate_refresh_token(user_id);
        }
        );
}

seastar::future<std::optional<AuthUserClaims>>
AuthService::verify_token_async(
    const std::string& token,
    IBlockingExecutor& executor) const
{
    /**
     * Vérifie le token hors reactor.
     *
     * À utiliser dans ProtectedHandler/AuthMiddleware si tu veux éliminer
     * les stalls liés à libcrypto.
     */
    return executor.submit(
        [this, token] {
            return verify_token(token);
        }
        );
}

seastar::future<std::optional<std::string>>
AuthService::verify_refresh_token_async(
    const std::string& token,
    IBlockingExecutor& executor) const
{
    /**
     * Vérifie le refresh token hors reactor.
     */
    return executor.submit(
        [this, token] {
            return verify_refresh_token(token);
        }
        );
}

} // namespace sea::application