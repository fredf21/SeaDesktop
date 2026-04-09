#include "security/secret_store.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace fs = std::filesystem;

namespace sea::infrastructure::security {

namespace {

[[nodiscard]] bool is_blank(const std::string& s) {
    return s.empty();
}

[[nodiscard]] std::string trim_right(std::string value) {
    while (!value.empty()) {
        const char c = value.back();
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') {
            value.pop_back();
        } else {
            break;
        }
    }
    return value;
}

[[nodiscard]] std::string normalize_service_name_for_env(const std::string& serviceName) {
    std::string out;
    out.reserve(serviceName.size() * 2);

    for (std::size_t i = 0; i < serviceName.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(serviceName[i]);

        if (std::isalnum(ch)) {
            out.push_back(static_cast<char>(std::toupper(ch)));
        } else {
            out.push_back('_');
        }
    }

    return out;
}

[[nodiscard]] std::string make_secret_file_path(
    const std::string& storageDir,
    const std::string& serviceName
    ) {
    if (storageDir.empty()) {
        throw std::runtime_error("storageDir cannot be empty");
    }

    if (serviceName.empty()) {
        throw std::runtime_error("serviceName cannot be empty");
    }

    fs::path dir(storageDir);
    fs::path file = dir / (serviceName + ".jwt.secret");
    return file.string();
}

} // namespace

std::string build_service_jwt_env_name(const std::string& serviceName) {
    if (serviceName.empty()) {
        throw std::runtime_error("Service name cannot be empty");
    }

    return "SEA_JWT_SECRET_" + normalize_service_name_for_env(serviceName);
}

std::string generate_secure_random_secret(std::size_t length) {
    if (length == 0) {
        throw std::runtime_error("Secret length must be greater than 0");
    }

    static constexpr char charset[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "-_";

    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<std::size_t> dist(0, sizeof(charset) - 2);

    std::string secret;
    secret.reserve(length);

    for (std::size_t i = 0; i < length; ++i) {
        secret.push_back(charset[dist(gen)]);
    }

    return secret;
}

std::optional<std::string> load_secret_from_file(
    const std::string& storageDir,
    const std::string& serviceName
    ) {
    const auto filePath = make_secret_file_path(storageDir, serviceName);

    std::ifstream in(filePath, std::ios::in);
    if (!in.is_open()) {
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();

    std::string secret = trim_right(buffer.str());
    if (is_blank(secret)) {
        return std::nullopt;
    }

    return secret;
}

void save_secret_to_file(
    const std::string& storageDir,
    const std::string& serviceName,
    const std::string& secret
    ) {
    if (is_blank(secret)) {
        throw std::runtime_error("Cannot save empty JWT secret");
    }

    fs::create_directories(storageDir);

    const auto filePath = make_secret_file_path(storageDir, serviceName);

    {
        std::ofstream out(filePath, std::ios::out | std::ios::trunc);
        if (!out.is_open()) {
            throw std::runtime_error("Failed to open JWT secret file for writing: " + filePath);
        }

        out << secret;
        if (!out.good()) {
            throw std::runtime_error("Failed to write JWT secret file: " + filePath);
        }
    }

    std::error_code ec;
    fs::permissions(
        filePath,
        fs::perms::owner_read | fs::perms::owner_write,
        fs::perm_options::replace,
        ec
        );
}

std::string resolve_jwt_secret(const JwtSecretConfig& config) {
    if (config.serviceName.empty()) {
        throw std::runtime_error("serviceName is required");
    }

    if (config.storageDir.empty()) {
        throw std::runtime_error("storageDir is required");
    }

    const std::string envName = build_service_jwt_env_name(config.serviceName);

    if (const char* envValue = std::getenv(envName.c_str())) {
        std::string secret(envValue);
        if (!secret.empty()) {
            return secret;
        }
    }

    if (auto stored = load_secret_from_file(config.storageDir, config.serviceName)) {
        return *stored;
    }

    const std::string generated = generate_secure_random_secret();
    save_secret_to_file(config.storageDir, config.serviceName, generated);
    return generated;
}

} // namespace sea::infrastructure::security