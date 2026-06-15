#pragma once

#include <seastar/http/handlers.hh>
#include <memory>
#include <string>

namespace sea::http::handlers::admin {

/**
 * @brief Handler pour POST /admin/projects/{file}.
 *
 * Cree un nouveau fichier YAML projet dans le dossier configs/ du
 * serveur. Avant ecriture, le YAML est valide via
 * ImportYamlSchemaUseCase pour garantir qu'il decrit un projet
 * SeaDesktop valide. Un fichier YAML invalide ne sera jamais persiste.
 *
 * Difference avec PUT /admin/projects/{file} :
 *   - POST : refuse si le fichier existe deja (409 Conflict)
 *   - PUT  : refuse si le fichier n'existe pas (404 Not Found)
 *
 * IMPORTANT : cet endpoint NE redemarre PAS le service et n'ajoute
 * PAS automatiquement un container pour le nouveau projet en mode
 * Docker. Le nouveau YAML est present sur le disque mais le client
 * doit orchestrer manuellement le lancement d'un nouveau service
 * (limitation v1.0).
 *
 * Strategie d'ecriture atomique :
 *   1. Lecture du body (YAML envoye par le client).
 *   2. Verification que le fichier n'existe pas encore.
 *   3. Ecriture dans un fichier temporaire <file>.tmp.
 *   4. Validation via ImportYamlSchemaUseCase sur le tmp.
 *   5. Verification que project.name correspond au basename du file.
 *   6. Si tout OK : rename atomique tmp -> final.
 *      Si KO : suppression du tmp, retour 400/409 avec message.
 *
 * Securite : role admin requis. Le X-User-Role est injecte par
 * ProtectedHandler en amont apres verification du JWT.
 *
 * Codes d'erreur :
 *   - 400 : nom de fichier invalide, YAML invalide, ou nom incoherent
 *   - 401 : JWT manquant ou invalide
 *   - 403 : utilisateur authentifie mais role non-admin
 *   - 409 : un fichier de ce nom existe deja (utiliser PUT pour
 *           remplacer)
 *   - 500 : erreur de lecture/ecriture filesystem
 *
 * Reponse succes :
 *   201 Created
 *   {"success":true,"file":"NewProject.yaml"}
 */
class CreateProjectHandler final : public seastar::httpd::handler_base {
public:
    /**
     * @param configs_dir Dossier ou creer les YAML (resolu une
     *                    fois au demarrage via resolve_configs_dir).
     * @param admin_role  Nom du role admin (configurable via YAML
     *                    access_control.admin_role).
     */
    CreateProjectHandler(std::string configs_dir,
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