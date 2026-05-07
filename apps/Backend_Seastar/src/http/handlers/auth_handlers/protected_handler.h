#pragma once
#include "authservice.h"
#include "thread_pool_execution/i_blocking_executor.h"
#include <seastar/http/httpd.hh>
#include <memory>

namespace sea::application { class AuthService; }

namespace sea::http::handlers::auth {

class ProtectedHandler final : public seastar::httpd::handler_base {
public:
    ProtectedHandler(std::unique_ptr<seastar::httpd::handler_base> inner,
                     std::shared_ptr<sea::application::AuthService> auth_service, std::shared_ptr<IBlockingExecutor> blocking_executor);

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring& path,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override;

private:
    std::unique_ptr<seastar::httpd::handler_base> inner_;
    std::shared_ptr<sea::application::AuthService> auth_service_;
    std::shared_ptr<IBlockingExecutor> blocking_executor_;

    /**
     * Strip tous les headers X-User-* venant du client.
     *
     * SÉCURITÉ : empêche un attaquant de forger X-User-Role: admin
     * pour bypasser l'autorisation côté middleware downstream.
     */
    void strip_user_headers(seastar::http::request& req) const;

    /**
     * Injecte les claims du JWT vérifié comme headers X-User-*.
     *
     * Headers générés :
     *   X-User-Id            : user_id
     *   X-User-Email         : email
     *   X-User-Role          : role
     *   X-User-<Custom>      : tous les claims custom du JWT
     *                          (department_id → X-User-Department-Id)
     */
    void inject_claims_as_headers(
        seastar::http::request& req,
        const sea::application::AuthUserClaims& claims) const;

    /**
     * Convertit "department_id" en "Department-Id".
     */
    static std::string to_header_case(const std::string& claim_name);
};

std::unique_ptr<seastar::httpd::handler_base> maybe_protect(
    std::unique_ptr<seastar::httpd::handler_base> handler,
    bool requires_auth,
    const std::shared_ptr<sea::application::AuthService>& auth_service, const std::shared_ptr<IBlockingExecutor>& blocking_executor );

}
