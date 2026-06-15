#pragma once

#include <seastar/http/handlers.hh>
#include <memory>
#include <string>

namespace sea::http::handlers::admin {

/**
 * @brief Handler pour DELETE /admin/projects/{file}.
 *
 * Supprime un fichier YAML projet du dossier configs/ du serveur. La
 * suppression est definitive : pas de corbeille, pas de backup
 * automatique. Le client (typiquement SeaUI) est responsable de
 * demander confirmation a l'utilisateur avant d'envoyer la requete.
 *
 * IMPORTANT : cet endpoint NE redemarre PAS le service et n'arrete
 * PAS automatiquement un container qui tournerait avec ce YAML. Si
 * un service est en cours d'execution avec ce YAML :
 *   - Le service continue de tourner avec son YAML deja charge en RAM.
 *   - Au prochain redemarrage, le service echouera a charger son YAML
 *     (fichier introuvable).
 * Le client doit donc orchestrer l'arret du container avant la
 * suppression du YAML (limitation v1.0).
 *
 * Securite : role admin requis. Le X-User-Role est injecte par
 * ProtectedHandler en amont apres verification du JWT.
 *
 * Validation du parametre {file} :
 *   - Caracteres interdits (refus rapide).
 *   - Canonical check anti path-traversal.
 *
 * Codes d'erreur :
 *   - 400 : nom de fichier invalide ou path traversal detecte
 *   - 401 : JWT manquant ou invalide
 *   - 403 : utilisateur authentifie mais role non-admin
 *   - 404 : fichier introuvable dans configs_dir
 *   - 500 : erreur de suppression filesystem
 *
 * Reponse succes :
 *   200 OK
 *   {"success":true,"file":"TestDemo.yaml"}
 */
class DeleteProjectHandler final : public seastar::httpd::handler_base {
public:
    /**
     * @param configs_dir Dossier ou chercher les YAML (resolu une
     *                    fois au demarrage via resolve_configs_dir).
     * @param admin_role  Nom du role admin (configurable via YAML
     *                    access_control.admin_role).
     */
    DeleteProjectHandler(std::string configs_dir,
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