#include "authservice.h"
#include <nlohmann/json.hpp>
#include <string>
#include <functional>
#include "bcrypt/BCrypt.hpp"
namespace sea::application {
namespace {
    using json = nlohmann::json;

    std::string simple_hash(const std::string& input) {
        return std::to_string(std::hash<std::string>{}(input));
    }
}

sea::application::AuthService::AuthService(std::string jwt_secret): jwt_secret_(std::move(jwt_secret))
{

}

std::string sea::application::AuthService::hash_password(const std::string &plain_password) const
{
    return BCrypt::generateHash(plain_password);
    throw std::runtime_error("hash_password() bcrypt non branche au wrapper.");
}

bool sea::application::AuthService::verify_password(const std::string &plain_password, const std::string &hashed_password) const
{
    return BCrypt::validatePassword(plain_password, hashed_password);
    throw std::runtime_error("verify_password() bcrypt non branche au wrapper.");
}

std::string sea::application::AuthService::generate_token(const std::string &user_id, const std::string &email) const
{
    // MVP token simple pour l architecture actuelle.
    // Plus tard je remplacerai par un vrai JWT.
    json payload = {
        {"user_id", user_id},
        {"email", email},
        {"signature", user_id + "|" + email + "|" + jwt_secret_}
    };
    std::string raw_token = payload.dump();
    std::string encoded_token = base64_encode(raw_token);
    return encoded_token;
}

std::string AuthService::base64_encode(const std::string &input) const
{
    std::string output;
    int val = 0, valb = -6;

    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;

        while (valb >= 0) {
            output.push_back(base64_chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }

    if (valb > -6)
        output.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);

    while (output.size() % 4)
        output.push_back('=');

    return output;
}

std::string AuthService::base64_decode(const std::string &input) const
{
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++)
        T[base64_chars[i]] = i;

    std::string output;
    int val = 0, valb = -8;

    for (unsigned char c : input) {
        if (T[c] == -1) break;

        val = (val << 6) + T[c];
        valb += 6;

        if (valb >= 0) {
            output.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }

    return output;
}

std::optional<sea::application::AuthUserClaims> sea::application::AuthService::verify_token(const std::string &token) const
{
    try {
        std::string decoded = base64_decode(token);
        auto payload = json::parse(decoded);

        const auto user_id = payload.at("user_id").get<std::string>();
        const auto email = payload.at("email").get<std::string>();
        const auto signature = payload.at("signature").get<std::string>();

        const auto expected = user_id + "|" + email + "|" + jwt_secret_;
        if (signature != expected) {
            return std::nullopt;
        }

        return AuthUserClaims{
            .user_id = user_id,
            .email = email
        };
    } catch (...) {
        return std::nullopt;
    }
}
}