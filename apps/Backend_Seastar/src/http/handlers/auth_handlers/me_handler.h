#pragma once

#include <seastar/http/httpd.hh>
#include <memory>

namespace sea::infrastructure::runtime {
class GenericCrudEngine;
}

namespace sea::http::handlers::auth {

/**
 * MeHandler
 *
 * Route : GET /auth/me
 *
 * Responsabilité :
 * - récupérer l'utilisateur courant
 *
 * Important :
 * → suppose que l'authentification est déjà faite
 *   par ProtectedHandler (middleware)
 */
class MeHandler final : public seastar::httpd::handler_base {
public:
    explicit MeHandler(
        std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine
        );

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override;

private:
    /**
     * Accès aux opérations CRUD
     */
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine_;
};

} // namespace sea::http::handlers::auth