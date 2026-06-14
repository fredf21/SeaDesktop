#pragma once

#include <seastar/http/httpd.hh>

#include <memory>
#include <string>

namespace sea::application::logging {
class RingBufferSink;
}

namespace sea::http::handlers::logs::admin {

/**
 * LogsHandler — GET /admin/logs
 *
 * Lit le ring buffer en memoire (cf RingBufferSink) et retourne les
 * logs au format JSON.
 *
 * Query params supportes :
 *   - limit       : max entrees retournees (default 100, max 1000)
 *   - level       : filtre niveau minimum (trace|debug|info|warn|error|critical)
 *   - logger      : filtre nom du logger exact (ex: "sea.http")
 *   - since       : retourne uniquement les logs apres ce sequence_id
 *   - search      : substring case-insensitive dans le message
 *
 * Reponse JSON :
 *   {
 *     "logs": [
 *       { "sequence_id": 12345, "timestamp": "...", "logger": "sea.http",
 *         "level": "info", "message": "..." },
 *       ...
 *     ],
 *     "count": 100,
 *     "next_sequence_id": 12500,
 *     "buffer_size": 7234,
 *     "buffer_capacity": 10000
 *   }
 *
 * Le client peut ensuite faire un re-fetch avec `since=12500` pour
 * recuperer uniquement les nouveaux logs (polling efficace).
 *
 * Securite : ProtectedHandler (auth) + garde admin role dans le handler.
 * Le nom du role admin est configurable via le YAML
 * (authorization.admin_role), passe via le constructeur.
 */
class LogsHandler final : public seastar::httpd::handler_base {
public:
    /**
     * @param sink       Le ring buffer a interroger
     * @param admin_role Le nom du role qui peut acceder a la route.
     *                   Provient de service.access_control.admin_role()
     *                   (configurable via YAML, default "admin").
     */
    LogsHandler(
        std::shared_ptr<sea::application::logging::RingBufferSink> sink,
        std::string admin_role
        );

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring& path,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override;

private:
    std::shared_ptr<sea::application::logging::RingBufferSink> sink_;
    std::string                                                 admin_role_;
};


/**
 * LoggersListHandler — GET /admin/logs/loggers
 *
 * Retourne la liste des loggers connus (les 7 noms pre-declares dans
 * LoggingInitializer). Utile pour populer un dropdown de filtre dans SeaUI.
 *
 * Reponse JSON :
 *   { "loggers": ["sea.boot", "sea.http", "sea.application", ...] }
 *
 * Securite : meme principe que LogsHandler (admin role configurable).
 */
class LoggersListHandler final : public seastar::httpd::handler_base {
public:
    /**
     * @param admin_role Le nom du role qui peut acceder a la route.
     */
    explicit LoggersListHandler(std::string admin_role);

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring& path,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override;

private:
    std::string admin_role_;
};

} // namespace sea::http::handlers::logs::admin