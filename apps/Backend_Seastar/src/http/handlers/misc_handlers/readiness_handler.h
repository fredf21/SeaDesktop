#pragma once

#include "persistence/i_generic_repository.h"
#include "storage/i_file_storage.h"

#include <seastar/http/handlers.hh>

#include <memory>

namespace sea::http::handlers::misc {

/**
 * ReadinessHandler
 *
 * Route : GET /health/ready
 *
 * Verifie que le service est PRET a traiter des requetes en
 * controlant ses dependances critiques :
 *   - Base de donnees : execute une transaction no-op pour valider
 *     que la connexion fonctionne (BEGIN; COMMIT;).
 *   - Storage de fichiers (optionnel) : verifie l'acces au backend
 *     de stockage en appelant exists() sur un path quelconque.
 *
 * Codes retour :
 *   - 200 OK              : toutes les dependances repondent
 *   - 503 Service Unavail : au moins une dependance est en erreur
 *
 * Distinction avec /health :
 *   - /health         : "le process est vivant" (liveness probe)
 *   - /health/ready   : "le service peut traiter" (readiness probe)
 *
 * Le storage est optionnel : si nullptr, il n'est pas verifie et
 * n'apparait pas dans la reponse.
 */
class ReadinessHandler final : public seastar::httpd::handler_base {
public:
    /**
     * @param repository  Repository pour le check DB (obligatoire).
     * @param storage     Storage pour le check filesystem (optionnel,
     *                    nullptr accepte si le service n'expose pas
     *                    de gestion de fichiers).
     */
    ReadinessHandler(
        std::shared_ptr<sea::infrastructure::persistence::IGenericRepository> repository,
        std::shared_ptr<sea::infrastructure::storage::IFileStorage> storage);

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring& path,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override;

private:
    std::shared_ptr<sea::infrastructure::persistence::IGenericRepository> repository_;
    std::shared_ptr<sea::infrastructure::storage::IFileStorage> storage_;
};

} // namespace sea::http::handlers::misc