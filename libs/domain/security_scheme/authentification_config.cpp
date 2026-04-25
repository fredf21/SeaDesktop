#include "authentification_config.h"

#include <stdexcept>

namespace sea::domain::security {

using namespace std::chrono_literals;

// ===== Conversions string <-> enum =====

AuthType auth_type_from_string(std::string_view s)
{
    if (s == "none")    return AuthType::None;
    if (s == "jwt")     return AuthType::Jwt;
    if (s == "api_key") return AuthType::ApiKey;
    if (s == "basic")   return AuthType::Basic;
    if (s == "oauth2")  return AuthType::OAuth2;
    throw std::invalid_argument(
        "AuthentificationConfig: unknown auth type '" + std::string(s) + "'"
        );
}

JwtAlgorithm jwt_algorithm_from_string(std::string_view s)
{
    if (s == "HS256") return JwtAlgorithm::HS256;
    if (s == "HS384") return JwtAlgorithm::HS384;
    if (s == "HS512") return JwtAlgorithm::HS512;
    if (s == "RS256") return JwtAlgorithm::RS256;
    if (s == "RS384") return JwtAlgorithm::RS384;
    if (s == "RS512") return JwtAlgorithm::RS512;
    if (s == "ES256") return JwtAlgorithm::ES256;
    if (s == "ES384") return JwtAlgorithm::ES384;
    if (s == "ES512") return JwtAlgorithm::ES512;
    throw std::invalid_argument(
        "AuthentificationConfig: unknown JWT algorithm '" + std::string(s) + "'"
        );
}

// ===== Constructeur =====

AuthentificationConfig::AuthentificationConfig()
    : type_(AuthType::None)
    , jwt_algorithm_(JwtAlgorithm::HS256)
    , jwt_secret_{}
    , jwt_public_key_path_{}
    , jwt_private_key_path_{}
    , jwt_issuer_{}
    , jwt_audience_{}
    , access_token_ttl_(15min)        // 15 minutes par défaut
    , refresh_token_ttl_(24h * 7)     // 7 jours par défaut
    , api_key_header_name_("X-API-Key")
    , oauth2_issuer_url_{}
    , oauth2_jwks_url_{}
{
}

// ===== Setters =====

AuthentificationConfig& AuthentificationConfig::set_type(AuthType type)
{
    type_ = type;
    return *this;
}

AuthentificationConfig& AuthentificationConfig::set_jwt_algorithm(JwtAlgorithm algo)
{
    jwt_algorithm_ = algo;
    return *this;
}

AuthentificationConfig& AuthentificationConfig::set_jwt_secret(std::string secret)
{
    jwt_secret_ = std::move(secret);
    return *this;
}

AuthentificationConfig& AuthentificationConfig::set_jwt_public_key_path(std::string path)
{
    jwt_public_key_path_ = std::move(path);
    return *this;
}

AuthentificationConfig& AuthentificationConfig::set_jwt_private_key_path(std::string path)
{
    jwt_private_key_path_ = std::move(path);
    return *this;
}

AuthentificationConfig& AuthentificationConfig::set_jwt_issuer(std::string issuer)
{
    jwt_issuer_ = std::move(issuer);
    return *this;
}

AuthentificationConfig& AuthentificationConfig::set_jwt_audience(std::string audience)
{
    jwt_audience_ = std::move(audience);
    return *this;
}

AuthentificationConfig& AuthentificationConfig::set_access_token_ttl(std::chrono::seconds ttl)
{
    access_token_ttl_ = ttl;
    return *this;
}

AuthentificationConfig& AuthentificationConfig::set_refresh_token_ttl(std::chrono::seconds ttl)
{
    refresh_token_ttl_ = ttl;
    return *this;
}

AuthentificationConfig& AuthentificationConfig::set_api_key_header_name(std::string header)
{
    api_key_header_name_ = std::move(header);
    return *this;
}

AuthentificationConfig& AuthentificationConfig::set_oauth2_issuer_url(std::string url)
{
    oauth2_issuer_url_ = std::move(url);
    return *this;
}

AuthentificationConfig& AuthentificationConfig::set_oauth2_jwks_url(std::string url)
{
    oauth2_jwks_url_ = std::move(url);
    return *this;
}

// ===== Getters =====

AuthType AuthentificationConfig::type() const { return type_; }
JwtAlgorithm AuthentificationConfig::jwt_algorithm() const { return jwt_algorithm_; }
const std::string& AuthentificationConfig::jwt_secret() const { return jwt_secret_; }
const std::string& AuthentificationConfig::jwt_public_key_path() const { return jwt_public_key_path_; }
const std::string& AuthentificationConfig::jwt_private_key_path() const { return jwt_private_key_path_; }
const std::string& AuthentificationConfig::jwt_issuer() const { return jwt_issuer_; }
const std::string& AuthentificationConfig::jwt_audience() const { return jwt_audience_; }
std::chrono::seconds AuthentificationConfig::access_token_ttl() const { return access_token_ttl_; }
std::chrono::seconds AuthentificationConfig::refresh_token_ttl() const { return refresh_token_ttl_; }
const std::string& AuthentificationConfig::api_key_header_name() const { return api_key_header_name_; }
const std::string& AuthentificationConfig::oauth2_issuer_url() const { return oauth2_issuer_url_; }
const std::string& AuthentificationConfig::oauth2_jwks_url() const { return oauth2_jwks_url_; }

// ===== Helpers =====

bool AuthentificationConfig::is_enabled() const
{
    return type_ != AuthType::None;
}

bool AuthentificationConfig::uses_symmetric_key() const
{
    if (type_ != AuthType::Jwt) return false;
    return jwt_algorithm_ == JwtAlgorithm::HS256
           || jwt_algorithm_ == JwtAlgorithm::HS384
           || jwt_algorithm_ == JwtAlgorithm::HS512;
}

bool AuthentificationConfig::uses_asymmetric_key() const
{
    if (type_ != AuthType::Jwt) return false;
    return !uses_symmetric_key();
}

// ===== Validation =====

void AuthentificationConfig::validate() const
{
    switch (type_) {
    case AuthType::None:
        // Rien à valider, auth désactivée
        break;

    case AuthType::Jwt:
        // JWT avec clé symétrique : secret requis
        if (uses_symmetric_key()) {
            if (jwt_secret_.empty()) {
                throw std::invalid_argument(
                    "AuthentificationConfig: JWT with symmetric algorithm "
                    "(HS256/HS384/HS512) requires a secret"
                    );
            }
            if (jwt_secret_.size() < 32) {
                throw std::invalid_argument(
                    "AuthentificationConfig: JWT secret must be at least 32 chars "
                    "(use a cryptographically random string)"
                    );
            }
        }
        // JWT avec clé asymétrique : public key path requis
        if (uses_asymmetric_key()) {
            if (jwt_public_key_path_.empty()) {
                throw std::invalid_argument(
                    "AuthentificationConfig: JWT with asymmetric algorithm "
                    "(RS*/ES*) requires a public_key_path"
                    );
            }
            // private_key_path n'est requis que si on émet des tokens
            // (pas si on ne fait que valider). On laisse la validation à
            // la couche infrastructure qui sait ce dont elle a besoin.
        }
        // TTL cohérents
        if (access_token_ttl_.count() <= 0) {
            throw std::invalid_argument(
                "AuthentificationConfig: access_token_ttl must be > 0"
                );
        }
        if (refresh_token_ttl_.count() <= 0) {
            throw std::invalid_argument(
                "AuthentificationConfig: refresh_token_ttl must be > 0"
                );
        }
        if (refresh_token_ttl_ < access_token_ttl_) {
            throw std::invalid_argument(
                "AuthentificationConfig: refresh_token_ttl must be >= access_token_ttl "
                "(otherwise refresh tokens are useless)"
                );
        }
        // TTL d'access token > 24h = mauvaise pratique
        if (access_token_ttl_ > std::chrono::hours(24)) {
            throw std::invalid_argument(
                "AuthentificationConfig: access_token_ttl > 24h is a security risk. "
                "Use a shorter TTL (15min recommended) and rely on refresh tokens"
                );
        }
        break;

    case AuthType::ApiKey:
        if (api_key_header_name_.empty()) {
            throw std::invalid_argument(
                "AuthentificationConfig: API Key auth requires a header name"
                );
        }
        break;

    case AuthType::Basic:
        // HTTP Basic Auth : aucun champ obligatoire dans la config
        // (la validation se fait contre une base d'utilisateurs)
        // Mais on peut warn que c'est une mauvaise pratique...
        break;

    case AuthType::OAuth2:
        if (oauth2_issuer_url_.empty()) {
            throw std::invalid_argument(
                "AuthentificationConfig: OAuth2 requires issuer_url"
                );
        }
        if (oauth2_jwks_url_.empty()) {
            throw std::invalid_argument(
                "AuthentificationConfig: OAuth2 requires jwks_url"
                );
        }
        break;
    }
}

// ===== Factories =====

AuthentificationConfig AuthentificationConfig::none()
{
    AuthentificationConfig config;
    config.set_type(AuthType::None);
    return config;
}

AuthentificationConfig AuthentificationConfig::jwt_with_secret(std::string secret,
                                                               JwtAlgorithm algo)
{
    AuthentificationConfig config;
    config.set_type(AuthType::Jwt);
    config.set_jwt_algorithm(algo);
    config.set_jwt_secret(std::move(secret));
    return config;
}

AuthentificationConfig AuthentificationConfig::jwt_with_keys(std::string public_key_path,
                                                             std::string private_key_path,
                                                             JwtAlgorithm algo)
{
    AuthentificationConfig config;
    config.set_type(AuthType::Jwt);
    config.set_jwt_algorithm(algo);
    config.set_jwt_public_key_path(std::move(public_key_path));
    config.set_jwt_private_key_path(std::move(private_key_path));
    return config;
}

AuthentificationConfig AuthentificationConfig::api_key(std::string header_name)
{
    AuthentificationConfig config;
    config.set_type(AuthType::ApiKey);
    config.set_api_key_header_name(std::move(header_name));
    return config;
}

} // namespace sea::domain::security