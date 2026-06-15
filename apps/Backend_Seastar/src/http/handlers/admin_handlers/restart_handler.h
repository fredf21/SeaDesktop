#pragma once

#include <seastar/http/handlers.hh>
#include <memory>
#include <string>

namespace sea::http::handlers::admin {

/**
 * @brief Handler pour POST /admin/restart.
 *
 * Termine proprement le process backend afin que Docker (avec la
 * directive restart: unless-stopped dans le docker-compose) le
 * redemarre automatiquement. Le nouveau process recharge les YAML
 * depuis le disque, ce qui applique les changements faits via
 * PUT /admin/projects/{file}.
 *
 * Strategie : on retourne d'abord la reponse HTTP au client, puis on
 * planifie l'arret du process apres un court delai (500ms). Cela
 * laisse le temps au buffer TCP de transmettre la reponse complete
 * avant que le process ne meurt.
 *
 * Pourquoi pas exit(0) immediat : si on appelle exit avant que la
 * reponse soit reellement envoyee sur le socket, le client recoit une
 * connexion fermee brutalement plutot qu'une reponse HTTP propre.
 *
 * IMPORTANT : cet endpoint suppose que Docker (ou un orchestrateur
 * equivalent) est configure pour redemarrer le container apres
 * exit(0). En execution locale sans Docker, le service mourra
 * definitivement.
 *
 * Securite : role admin requis. Le X-User-Role est injecte par
 * ProtectedHandler en amont apres verification du JWT.
 *
 * Codes d'erreur :
 *   - 401 : JWT manquant ou invalide
 *   - 403 : utilisateur authentifie mais role non-admin
 *
 * Reponse succes :
 *   202 Accepted
 *   {"success":true,"message":"Service restarting"}
 */
class RestartHandler final : public seastar::httpd::handler_base {
public:
    /**
     * @param admin_role Nom du role admin (configurable via YAML
     *                   access_control.admin_role).
     */
    explicit RestartHandler(std::string admin_role);

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring& path,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override;

private:
    std::string admin_role_;
};

} // namespace sea::http::handlers::admin