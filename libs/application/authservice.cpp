#include "authservice.h"

#include "security/jwt_service.h"

#include <bcrypt/BCrypt.hpp>

#include <stdexcept>
#include <utility>

namespace sea::application {

AuthService::AuthService(std::string jwt_secret)
    : jwt_secret_(std::move(jwt_secret))
{
    if (jwt_secret_.empty()) {
        throw std::runtime_error("JWT secret cannot be empty");
    }
}

std::string AuthService::hash_password(const std::string& plain_password) const {
    return BCrypt::generateHash(plain_password);
}

bool AuthService::verify_password(const std::string& plain_password,
                                  const std::string& hashed_password) const {
    return BCrypt::validatePassword(plain_password, hashed_password);
}

std::string AuthService::generate_token(const std::string& user_id,
                                        const std::string& email,
                                        const std::string& role,
                                        std::int64_t expires_in_seconds) const {
    return sea::infrastructure::security::JwtService::generate_token(
        user_id,
        email,
        role,
        jwt_secret_,
        expires_in_seconds
    );
}

std::optional<AuthUserClaims> AuthService::verify_token(const std::string& token) const {
    const auto claims =
        sea::infrastructure::security::JwtService::verify_token(token, jwt_secret_);

    if (!claims.has_value()) {
        return std::nullopt;
    }

    AuthUserClaims result;
    result.user_id = claims->userId;
    result.email = claims->email;
    result.role = claims->role;

    return result;
}

} // namespace sea::application