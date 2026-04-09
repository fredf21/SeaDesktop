#include "security/jwt_service.h"

#include <jwt-cpp/jwt.h>

#include <chrono>
#include <stdexcept>

namespace sea::infrastructure::security {

std::string JwtService::generate_token(
    const std::string& userId,
    const std::string& email,
    const std::string& role,
    const std::string& secret,
    std::int64_t expiresInSeconds
    ) {
    if (userId.empty()) {
        throw std::runtime_error("userId cannot be empty");
    }

    if (secret.empty()) {
        throw std::runtime_error("JWT secret cannot be empty");
    }

    if (expiresInSeconds <= 0) {
        throw std::runtime_error("expiresInSeconds must be greater than 0");
    }

    const auto now = std::chrono::system_clock::now();
    const auto exp = now + std::chrono::seconds(expiresInSeconds);

    return jwt::create()
        .set_type("JWT")
        .set_issued_at(now)
        .set_expires_at(exp)
        .set_subject(userId)
        .set_payload_claim("email", jwt::claim(email))
        .set_payload_claim("role", jwt::claim(role))
        .sign(jwt::algorithm::hs256{secret});
}

std::optional<JwtClaims> JwtService::verify_token(
    const std::string& token,
    const std::string& secret
    ) {
    if (token.empty() || secret.empty()) {
        return std::nullopt;
    }

    try {
        const auto decoded = jwt::decode(token);

        jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{secret})
            .with_type("JWT")
            .verify(decoded);

        JwtClaims claims;
        claims.userId = decoded.get_subject();

        if (decoded.has_payload_claim("email")) {
            claims.email = decoded.get_payload_claim("email").as_string();
        }

        if (decoded.has_payload_claim("role")) {
            claims.role = decoded.get_payload_claim("role").as_string();
        }

        return claims;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::string> extract_bearer_token(const std::string& authorizationHeader) {
    static const std::string prefix = "Bearer ";

    if (authorizationHeader.rfind(prefix, 0) != 0) {
        return std::nullopt;
    }

    std::string token = authorizationHeader.substr(prefix.size());
    if (token.empty()) {
        return std::nullopt;
    }

    return token;
}

} // namespace sea::infrastructure::security