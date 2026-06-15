#pragma once

#include <seastar/http/handlers.hh>
#include <memory>
#include <string>

namespace sea::http::handlers::admin {

/**
 * @brief Handler pour PUT /admin/projects/{file}.
 *
 * Sauvegarde le contenu d'un fichier YAML existant dans le dossier
 * configs/ du serveur. Avant ecriture, le YAML est valide via
 * ImportYamlSchemaUseCase pour garantir qu'il decrit un projet
 * SeaDesktop valide. Un fichier YAML invalide ne sera jamais persiste.
 *
 * IMPORTANT : cet endpoint NE redemarre PAS le service. Le YAML en
 * cours d'execution dans la RAM du backend n'est pas affecte. Pour
 * appliquer les changements, le client doit appeler ensuite
 * POST /admin/restart (Phase 2 du chantier remote-first).
 *
 * Strategie d'ecriture atomique :
 *   1. Lecture du body (YAML envoye par le client).
 *   2. Ecriture dans un fichier temporaire <file>.tmp.
 *   3. Validation via ImportYamlSchemaUseCase sur le tmp.
 *   4. Si OK : rename atomique tmp -> final.
 *      Si KO : suppression du tmp, retour 400 avec message d'erreur.
 *
 * Pourquoi cette strategie : si le backend crash pendant l'ecriture
 * (kill, OOM, panne), on n'a jamais un YAML tronque sur disque. Soit
 * l'ancien YAML est intact (crash avant rename), soit le nouveau est
 * complet (crash apres rename). Aucun etat intermediaire.
 *
 * Securite : role admin requis. Le X-User-Role est injecte par
 * ProtectedHandler en amont apres verification du JWT.
 *
 * Le parametre {file} dans l'URL doit etre le nom complet du fichier
 * tel qu'il apparait dans la reponse de GET /admin/projects.
 *
 * Codes d'erreur :
 *   - 400 : nom de fichier invalide OU YAML invalide (syntaxe/schema)
 *   - 401 : JWT manquant ou invalide
 *   - 403 : utilisateur authentifie mais role non-admin
 *   - 404 : fichier introuvable (PUT remplace seulement, pas de
 *           creation -- utiliser POST /admin/projects pour creer)
 *   - 500 : erreur de lecture/ecriture filesystem
 *
 * Reponse succes :
 *   200 OK
 *   {"success":true,"file":"TestDemo.yaml"}
 */
class SaveProjectHandler final : public seastar::httpd::handler_base {
public:
    /**
     * @param configs_dir Dossier ou chercher les YAML (resolu une
     *                    fois au demarrage via resolve_configs_dir).
     * @param admin_role  Nom du role admin (configurable via YAML
     *                    access_control.admin_role).
     */
    SaveProjectHandler(std::string configs_dir,
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