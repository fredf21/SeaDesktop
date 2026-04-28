#pragma once
// sea_infrastructure/security/jwt_service.h

#include <chrono>
#include <optional>
#include <string>

namespace sea::infrastructure::security {

// Type de token (sera mis dans le claim "token_type")
enum class TokenType {
    Access,
    Refresh
};

// Claims complets décodés depuis un JWT
struct JwtClaims {
    std::string user_id;       // claim "sub"
    std::string email;          // claim "email"
    std::string role;           // claim "role"
    std::string issuer;         // claim "iss"
    TokenType token_type;       // claim "token_type"
    std::int64_t issued_at;     // claim "iat"
    std::int64_t expires_at;    // claim "exp"
};

// Paramètres pour générer un token
struct GenerateTokenParams {
    std::string user_id;
    std::string email;          // vide pour refresh tokens
    std::string role;           // vide pour refresh tokens
    std::string secret;
    std::string issuer;
    TokenType token_type;
    std::chrono::seconds ttl;
};

// Paramètres pour vérifier un token
struct VerifyTokenParams {
    std::string token;
    std::string secret;
    std::string expected_issuer;
    TokenType expected_type;    // Access ou Refresh
};

class JwtService {
public:
    // Génération
    [[nodiscard]] static std::string generate_token(
        const GenerateTokenParams& params);

    // Vérification (retourne les claims si valide, nullopt sinon)
    [[nodiscard]] static std::optional<JwtClaims> verify_token(
        const VerifyTokenParams& params);

    // Helpers de conversion
    [[nodiscard]] static std::string token_type_to_string(TokenType t) noexcept;
    [[nodiscard]] static std::optional<TokenType> token_type_from_string(
        const std::string& s) noexcept;
};

} // namespace sea::infrastructure::security