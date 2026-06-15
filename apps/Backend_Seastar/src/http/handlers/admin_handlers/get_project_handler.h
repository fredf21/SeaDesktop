#pragma once

#include <seastar/http/handlers.hh>
#include <memory>
#include <string>

namespace sea::http::handlers::admin {

/**
 * @brief Handler pour GET /admin/projects/{file}.
 *
 * Retourne le contenu brut d'un fichier YAML du dossier configs/ du
 * serveur. Cet endpoint sert a SeaUI en mode "remote" pour recuperer
 * la definition complete d'un projet et l'afficher dans son editeur,
 * sans acceder au filesystem distant.
 *
 * Securite : role admin requis. Le X-User-Role est injecte par
 * ProtectedHandler en amont apres verification du JWT.
 *
 * Le parametre {file} dans l'URL doit etre le nom complet du fichier
 * tel qu'il apparait dans la reponse de GET /admin/projects (champ
 * "file"), par exemple "TestDemo.yaml" ou "BlogDemo.yml".
 *
 * Reponse succes :
 *   Content-Type: application/x-yaml
 *   <contenu brut du fichier YAML>
 *
 * Validation du parametre {file} :
 *   - Doit etre non vide
 *   - Doit avoir l'extension .yaml ou .yml (insensible a la casse)
 *   - Ne doit pas contenir '/', '\\', '..' ou commencer par '.'
 *   - Le chemin canonique du fichier doit rester dans configs_dir
 *     (protection contre path traversal via symlinks ou autre)
 *
 * Codes d'erreur :
 *   - 400 : nom de fichier invalide
 *   - 401 : JWT manquant ou invalide (par ProtectedHandler)
 *   - 403 : utilisateur authentifie mais role non-admin
 *   - 404 : fichier introuvable dans configs_dir
 *   - 500 : erreur de lecture filesystem
 */
class GetProjectHandler final : public seastar::httpd::handler_base {
public:
    /**
     * @param configs_dir Dossier ou chercher les YAML (resolu une
     *                    fois au demarrage via resolve_configs_dir).
     * @param admin_role  Nom du role admin (configurable via YAML
     *                    access_control.admin_role).
     */
    GetProjectHandler(std::string configs_dir,
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