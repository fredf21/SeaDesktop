#ifndef CORS_MIDDLEWARE_H
#define CORS_MIDDLEWARE_H

#include "security_scheme/cors_config.h"

#include <seastar/http/handlers.hh>
#include <seastar/http/reply.hh>
#include <seastar/http/request.hh>

#include <memory>
#include <string>

namespace sea::http::middlewares {

class CorsMiddleware : public seastar::httpd::handler_base {
public:
    CorsMiddleware(
        std::unique_ptr<seastar::httpd::handler_base> inner,
        sea::domain::security::CorsConfig config
        );

    seastar::future<std::unique_ptr<seastar::http::reply>> handle(
        const seastar::sstring& path,
        std::unique_ptr<seastar::http::request> req,
        std::unique_ptr<seastar::http::reply> rep
        ) override;

private:
    // Gère le preflight OPTIONS
    seastar::future<std::unique_ptr<seastar::http::reply>> handle_preflight(
        const std::string& origin,
        std::unique_ptr<seastar::http::reply> rep
        );

    // Ajoute les headers CORS à la réponse
    void add_cors_headers(
        seastar::http::reply& rep,
        const std::string& origin
        ) const;

    // Récupère l'origin depuis la requête (vide si absent)
    std::string get_origin(const seastar::http::request& req) const;

    // Helpers de jointure pour les listes de strings/methods
    static std::string join_strings(const std::vector<std::string>& items);
    static std::string join_methods(
        const std::vector<sea::domain::security::HttpMethod>& methods);

    std::unique_ptr<seastar::httpd::handler_base> inner_;
    sea::domain::security::CorsConfig config_;
};

// Helper de chaînage
std::unique_ptr<seastar::httpd::handler_base> apply_cors(
    std::unique_ptr<seastar::httpd::handler_base> handler,
    sea::domain::security::CorsConfig config
    );

} // namespace sea::http::middlewares

#endif // CORS_MIDDLEWARE_H