#ifndef SEA_HTTP_MIDDLEWARES_AUTHORIZATION_MIDDLEWARE_H
#define SEA_HTTP_MIDDLEWARES_AUTHORIZATION_MIDDLEWARE_H

#include "access_control/policy_engine.h"
#include "route_authorization_resolver.h"

#include "access_control/access_control_config.h"
#include "access_control/policy_engine.h"
#include "schema.h"

#include <seastar/http/handlers.hh>
#include <seastar/http/reply.hh>
#include <seastar/http/request.hh>
#include <seastar/core/sstring.hh>
#include <memory>

namespace sea::http::middlewares {

/**
 * AuthorizationMiddleware
 *
 * Module 5 : Verifie les regles RBAC + ABAC (subject-only fast path)
 * AVANT l'appel au handler metier.
 *
 * Pipeline :
 *   ProtectedHandler (Module 4) → AuthorizationMiddleware (ICI) → CrudHandler
 *
 * Strategie C : pour les routes relationnelles, double check (parent + child).
 *
 * Flow :
 * 1. Resolve la route via RouteAuthorizationResolver
 *    - Routes inconnues → 403 (fail closed)
 * 2. Construit PolicySubject depuis les headers X-User-* (injectes par Module 4)
 * 3. Bypass admin si default_allow_admin=true et user a admin_role
 * 4. Pour chaque check du plan :
 *    a. Trouve l'AccessControlSpec dans l'entite
 *    b. Si pas de spec → applique default_policy
 *    c. Evalue avec PolicyEngine.evaluate_subject_only()
 *    d. Si refuse → log les autres checks restants puis 403 immediat
 *    e. Si OK + resource-aware → consulte abac_mode :
 *       - Permissive : continue (handler/Module 6 finalisera)
 *       - Strict     : 403 immediat
 * 5. Si tous passent → delegue au handler interne
 */
class AuthorizationMiddleware : public seastar::httpd::handler_base {
public:
    AuthorizationMiddleware(
        std::unique_ptr<seastar::httpd::handler_base> inner,
        const sea::domain::Schema* schema,
        const sea::domain::access_control::AccessControlConfig* config,
        std::shared_ptr<sea::application::access_control::PolicyEngine> policy_engine
        );

    seastar::future<std::unique_ptr<seastar::http::reply>> handle(
        const seastar::sstring& path,
        std::unique_ptr<seastar::http::request> req,
        std::unique_ptr<seastar::http::reply> rep
        ) override;

private:
    std::unique_ptr<seastar::httpd::handler_base> inner_;
    const sea::domain::Schema* schema_;
    const sea::domain::access_control::AccessControlConfig* config_;
    std::shared_ptr<sea::application::access_control::PolicyEngine> policy_engine_;
    RouteAuthorizationResolver resolver_;
};

/**
 * Helper pour wrapper un handler avec l'AuthorizationMiddleware.
 *
 * Si l'autorisation n'est pas activee dans la config, retourne le
 * handler tel quel (pass-through).
 */
std::unique_ptr<seastar::httpd::handler_base> apply_authorization(
    std::unique_ptr<seastar::httpd::handler_base> handler,
    const sea::domain::Schema& schema,
    const sea::domain::access_control::AccessControlConfig& config,
    std::shared_ptr<sea::application::access_control::PolicyEngine> policy_engine
    );

} // namespace sea::http::middlewares

#endif // SEA_HTTP_MIDDLEWARES_AUTHORIZATION_MIDDLEWARE_H