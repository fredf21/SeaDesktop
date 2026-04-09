#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace sea::infrastructure::security {

struct JwtClaims {
    std::string userId;
    std::string email;
    std::string role;
};

class JwtService {
public:
    [[nodiscard]] static std::string generate_token(
        const std::string& userId,
        const std::string& email,
        const std::string& role,
        const std::string& secret,
        std::int64_t expiresInSeconds = 3600
        );

    [[nodiscard]] static std::optional<JwtClaims> verify_token(
        const std::string& token,
        const std::string& secret
        );
};

[[nodiscard]] std::optional<std::string> extract_bearer_token(const std::string& authorizationHeader);

} // namespace sea::infrastructure::security