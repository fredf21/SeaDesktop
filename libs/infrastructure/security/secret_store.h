#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace sea::infrastructure::security {

struct JwtSecretConfig {
    std::string storageDir;
    std::string serviceName;
};

[[nodiscard]] std::string build_service_jwt_env_name(const std::string& serviceName);

[[nodiscard]] std::string generate_secure_random_secret(std::size_t length = 64);

[[nodiscard]] std::optional<std::string> load_secret_from_file(
    const std::string& storageDir,
    const std::string& serviceName
    );

void save_secret_to_file(
    const std::string& storageDir,
    const std::string& serviceName,
    const std::string& secret
    );

[[nodiscard]] std::string resolve_jwt_secret(const JwtSecretConfig& config);

} // namespace sea::infrastructure::security