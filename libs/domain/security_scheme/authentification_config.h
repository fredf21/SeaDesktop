#pragma once
// sea_domain/security_scheme/authentification_config.h

#include <chrono>
#include <string>
#include <string_view>

namespace sea::domain::security {

// Type d'authentification
enum class AuthType {
    None,       // Pas d'auth (dev/test ou service interne)
    Jwt,        // JSON Web Tokens (recommandé pour les API REST)
    ApiKey,     // Clé API simple (header X-API-Key)
    Basic,      // HTTP Basic Auth (legacy)
    OAuth2      // OAuth 2.0 délégué à un Identity Provider
};

// Algorithme de signature pour JWT
enum class JwtAlgorithm {
    HS256,      // HMAC-SHA256 (clé symétrique)
    HS384,
    HS512,
    RS256,      // RSA-SHA256 (clé asymétrique, recommandé)
    RS384,
    RS512,
    ES256,      // ECDSA-SHA256
    ES384,
    ES512
};

// Conversions enum <-> string
constexpr std::string_view to_string(AuthType t) noexcept
{
    switch (t) {
    case AuthType::None:    return "none";
    case AuthType::Jwt:     return "jwt";
    case AuthType::ApiKey:  return "api_key";
    case AuthType::Basic:   return "basic";
    case AuthType::OAuth2:  return "oauth2";
    }
    return "unknown";
}

constexpr std::string_view to_string(JwtAlgorithm a) noexcept
{
    switch (a) {
    case JwtAlgorithm::HS256: return "HS256";
    case JwtAlgorithm::HS384: return "HS384";
    case JwtAlgorithm::HS512: return "HS512";
    case JwtAlgorithm::RS256: return "RS256";
    case JwtAlgorithm::RS384: return "RS384";
    case JwtAlgorithm::RS512: return "RS512";
    case JwtAlgorithm::ES256: return "ES256";
    case JwtAlgorithm::ES384: return "ES384";
    case JwtAlgorithm::ES512: return "ES512";
    }
    return "unknown";
}

AuthType auth_type_from_string(std::string_view s);
JwtAlgorithm jwt_algorithm_from_string(std::string_view s);

class AuthentificationConfig {
public:
    // Constructeur par défaut : AuthType::None
    AuthentificationConfig();

    // Builder fluide
    AuthentificationConfig& set_type(AuthType type);

    // --- JWT ---
    AuthentificationConfig& set_jwt_algorithm(JwtAlgorithm algo);
    AuthentificationConfig& set_jwt_secret(std::string secret);              // pour HS*
    AuthentificationConfig& set_jwt_public_key_path(std::string path);       // pour RS*/ES*
    AuthentificationConfig& set_jwt_private_key_path(std::string path);      // pour RS*/ES*
    AuthentificationConfig& set_jwt_issuer(std::string issuer);
    AuthentificationConfig& set_jwt_audience(std::string audience);
    AuthentificationConfig& set_access_token_ttl(std::chrono::seconds ttl);
    AuthentificationConfig& set_refresh_token_ttl(std::chrono::seconds ttl);

    // --- API Key ---
    AuthentificationConfig& set_api_key_header_name(std::string header);

    // --- OAuth2 ---
    AuthentificationConfig& set_oauth2_issuer_url(std::string url);
    AuthentificationConfig& set_oauth2_jwks_url(std::string url);

    // Accesseurs
    AuthType type() const;

    JwtAlgorithm jwt_algorithm() const;
    const std::string& jwt_secret() const;
    const std::string& jwt_public_key_path() const;
    const std::string& jwt_private_key_path() const;
    const std::string& jwt_issuer() const;
    const std::string& jwt_audience() const;
    std::chrono::seconds access_token_ttl() const;
    std::chrono::seconds refresh_token_ttl() const;

    const std::string& api_key_header_name() const;

    const std::string& oauth2_issuer_url() const;
    const std::string& oauth2_jwks_url() const;

    // Helpers
    bool is_enabled() const;                  // type != None
    bool uses_symmetric_key() const;          // JWT HS*
    bool uses_asymmetric_key() const;         // JWT RS* ou ES*

    // Validation : vérifie que les champs requis pour le type sont présents
    void validate() const;

    // Factories
    static AuthentificationConfig none();
    static AuthentificationConfig jwt_with_secret(std::string secret,
                                                  JwtAlgorithm algo = JwtAlgorithm::HS256);
    static AuthentificationConfig jwt_with_keys(std::string public_key_path,
                                                std::string private_key_path,
                                                JwtAlgorithm algo = JwtAlgorithm::RS256);
    static AuthentificationConfig api_key(std::string header_name = "X-API-Key");

private:
    AuthType type_;

    // JWT
    JwtAlgorithm jwt_algorithm_;
    std::string jwt_secret_;
    std::string jwt_public_key_path_;
    std::string jwt_private_key_path_;
    std::string jwt_issuer_;
    std::string jwt_audience_;
    std::chrono::seconds access_token_ttl_;
    std::chrono::seconds refresh_token_ttl_;

    // API Key
    std::string api_key_header_name_;

    // OAuth2
    std::string oauth2_issuer_url_;
    std::string oauth2_jwks_url_;
};

} // namespace sea::domain::security