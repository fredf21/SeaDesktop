#ifndef AUTHSERVICE_H
#define AUTHSERVICE_H

#include <cstdint>
#include <optional>
#include <string>

namespace sea::application {

struct AuthUserClaims {
    std::string user_id;
    std::string email;
    std::string role;
};

class AuthService
{
public:
    explicit AuthService(std::string jwt_secret);

    [[nodiscard]] std::string hash_password(const std::string& plain_password) const;

    [[nodiscard]] bool verify_password(const std::string& plain_password,
                                       const std::string& hashed_password) const;

    [[nodiscard]] std::string generate_token(const std::string& user_id,
                                             const std::string& email,
                                             const std::string& role = "admin",
                                             std::int64_t expires_in_seconds = 3600) const;

    [[nodiscard]] std::optional<AuthUserClaims> verify_token(const std::string& token) const;

private:
    std::string jwt_secret_;
};

} // namespace sea::application

#endif // AUTHSERVICE_H