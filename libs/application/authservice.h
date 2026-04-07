#ifndef AUTHSERVICE_H
#define AUTHSERVICE_H

#include <optional>
#include <string>

namespace sea::application {
struct AuthUserClaims {
    std::string user_id;
    std::string email;
};

static const std::string base64_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

class AuthService
{
public:
    AuthService(std::string jwt_secret);
    [[nodiscard]] std::string hash_password(const std::string& plain_password) const;
    [[nodiscard]] bool verify_password(const std::string& plain_password,
                                       const std::string& hashed_password) const;

    [[nodiscard]] std::string generate_token(const std::string& user_id,
                                             const std::string& email) const;

    [[nodiscard]] std::optional<AuthUserClaims> verify_token(const std::string& token) const;
    std::string base64_encode(const std::string& input) const;
    std::string base64_decode(const std::string& input) const;

private:
    std::string jwt_secret_;
};
}
#endif // AUTHSERVICE_H
