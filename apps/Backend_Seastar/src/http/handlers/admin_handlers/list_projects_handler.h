#pragma once

#include <seastar/http/handlers.hh>
#include <memory>
#include <string>

namespace sea::http::handlers::admin {

/**
 * @brief Handler pour GET /admin/projects.
 *
 * Liste les projets YAML disponibles dans le dossier configs/ du
 * serveur. Cet endpoint sert a SeaUI en mode "remote" pour decouvrir
 * les projets gerables sans acceder au filesystem distant.
 *
 * Securite : role admin requis. Le X-User-Role est injecte par
 * ProtectedHandler en amont apres verification du JWT.
 *
 * Reponse JSON :
 *   {
 *     "projects": [
 *       { "name": "TestDemo", "file": "TestDemo.yaml" },
 *       { "name": "BlogDemo", "file": "BlogDemo.yaml" }
 *     ]
 *   }
 *
 * Le champ "name" est derive du nom de fichier sans extension. Le
 * champ "file" est le nom de fichier brut (utile pour les endpoints
 * /api/admin/projects/{file}).
 */
class ListProjectsHandler final : public seastar::httpd::handler_base {
public:
    /**
     * @param configs_dir Dossier ou chercher les YAML (resolu une
     *                    fois au demarrage via resolve_configs_dir).
     * @param admin_role  Nom du role admin (configurable via YAML
     *                    access_control.admin_role).
     */
    ListProjectsHandler(std::string configs_dir,
                        std::string admin_role);

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring& path,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override;

private:
    std::string configs_dir_;
    std::string admin_role_;
};

} // namespace sea::http::handlers::admin