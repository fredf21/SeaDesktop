#pragma once

#include <string>

namespace sea::http::handlers::admin {

/**
 * @brief Resout le dossier des projets YAML pour les endpoints admin.
 *
 * La resolution suit deux niveaux de priorite :
 *
 * 1. Variable d'environnement `SEA_DESKTOP_CONFIGS_DIR`.
 *    Permet d'override sans recompiler. Indispensable pour Docker,
 *    les CI, et les setups multi-machines.
 *
 * 2. Fallback : dossier parent du fichier YAML charge au demarrage.
 *    Si Backend_Seastar a ete lance avec
 *    `./backend_seastar configs/TestDemo.yaml`, le fallback retourne
 *    "configs/". C'est le comportement par defaut quand on lance
 *    Backend_Seastar en local sans configurer l'env var.
 *
 * Le chemin retourne est tel quel, sans normalisation. L'appelant peut
 * le passer a std::filesystem pour le canonicaliser si necessaire.
 *
 * @param loaded_yaml_path Chemin du YAML passe en argument au demarrage
 *                         de Backend_Seastar. Utilise uniquement pour
 *                         le fallback (niveau 2).
 * @return Chemin du dossier configs (peut etre relatif ou absolu).
 */
[[nodiscard]] std::string
resolve_configs_dir(const std::string& loaded_yaml_path);

} // namespace sea::http::handlers::admin
