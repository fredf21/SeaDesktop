#include "admin_paths.h"

#include <cstdlib>
#include <filesystem>

namespace sea::http::handlers::admin {

std::string resolve_configs_dir(const std::string& loaded_yaml_path)
{
    // Niveau 1 : variable d'environnement.
    if (const char* env = std::getenv("SEA_DESKTOP_CONFIGS_DIR");
        env != nullptr && env[0] != '\0') {
        return std::string(env);
    }

    // Niveau 2 : dirname du YAML charge au demarrage.
    // std::filesystem::path::parent_path() retourne "" si le path n'a
    // pas de slash (ex: "TestDemo.yaml" tout seul). On retombe alors
    // sur "." (dossier courant) pour rester sain.
    const std::filesystem::path p(loaded_yaml_path);
    const auto parent = p.parent_path();

    if (parent.empty()) {
        return ".";
    }
    return parent.string();
}

} // namespace sea::http::handlers::admin
