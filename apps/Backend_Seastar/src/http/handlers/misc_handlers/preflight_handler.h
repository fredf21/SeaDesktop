#pragma once

#include <seastar/http/httpd.hh>
#include <seastar/http/request.hh>
#include <seastar/http/reply.hh>

namespace sea::http::handlers::misc {

// ─────────────────────────────────────────────────────────────────
// PreflightHandler
//
// Handler trivial qui répond aux requêtes OPTIONS preflight CORS.
//
// Rationale : le CorsMiddleware sait gérer les preflight via
// handle_preflight(), mais il n'est invoqué que si le routeur
// Seastar matche une route. Aucune route OPTIONS n'étant
// enregistrée par défaut, le routeur renvoie 404 avant même que
// la chaîne de middlewares ne soit traversée.
//
// Ce handler résout ça : on enregistre OPTIONS pour chaque path
// CRUD/relations/M2M. Le routeur dispatche vers ce handler, qui
// passe par la chaîne de middlewares (donc par CorsMiddleware qui
// fera le vrai travail dans handle_preflight). Si CORS est
// désactivé ou si on n'est pas dans un preflight valide, ce
// handler retourne juste 204 No Content.
// ─────────────────────────────────────────────────────────────────
class PreflightHandler : public seastar::httpd::handler_base {
public:
    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request>,
           std::unique_ptr<seastar::http::reply> rep) override
    {
        rep->set_status(seastar::http::reply::status_type::no_content);
        return seastar::make_ready_future<std::unique_ptr<seastar::http::reply>>(
            std::move(rep));
    }
};

} // namespace sea::http::handlers
