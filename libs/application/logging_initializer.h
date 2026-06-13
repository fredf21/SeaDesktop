#pragma once

#include "logging/logging_config.h"
#include <memory>
namespace sea::application::logging {
class RingBufferSink;
}

namespace sea::application::logging {

/**
 * LoggingInitializer
 *
 * Service applicatif d'initialisation de spdlog au demarrage du
 * processus, et de teardown propre au shutdown.
 *
 * Strategie multi-shard : 1 logger partage entre shards (cf decision
 * etape 2.2). spdlog gere la concurrence en interne (spin locks).
 *
 * Loggers nommes pre-declares au demarrage :
 *   - sea.boot         : demarrage, migrations, initialisation
 *   - sea.http         : handlers HTTP, middlewares, routes
 *   - sea.application  : AuthService, TokenTrackingService, PolicyEngine
 *   - sea.persistence  : repositories MySQL/Postgres, schemas
 *   - sea.security     : JwtService, secret_store, hash/verify
 *   - seastar          : logs internes Seastar (via hook etape 2.3)
 *
 * Usage type :
 *
 *   // Au demarrage (apres parsing du YAML)
 *   LoggingInitializer::init(service.logging);
 *
 *   // Partout dans le code
 *   spdlog::get("sea.http")->info("login success: {}", email);
 *
 *   // Au shutdown propre
 *   LoggingInitializer::shutdown();
 *
 * Note : init() est idempotent — un second appel reconfigure les
 * loggers existants (utile si on recoit un signal pour recharger
 * la config).
 */
class LoggingInitializer {
public:
    /**
     * Initialise spdlog selon la config.
     *
     * Cree :
     *   - Le thread pool async (si config.async_config().enabled)
     *   - Les sinks (console, file rotatif)
     *   - Les 7 loggers nommes + le default_logger
     *   - Applique les niveaux par module
     *
     * @throws std::runtime_error si la config est invalide ou
     *         si la creation des sinks echoue (path inaccessible, etc.)
     *
     * Idempotent : un second appel reconfigure.
     */
    static void init(const sea::domain::logging::LoggingConfig& config);

    /**
     * Flush et drop tous les loggers.
     * A appeler au shutdown propre du processus.
     *
     * Apres shutdown(), les appels spdlog::get(...) retourneront nullptr.
     */
    static void shutdown();

    /**
     * Constantes des noms de loggers pour eviter les typos.
     * Usage : spdlog::get(LoggingInitializer::Loggers::http)
     *
     * Note : ce sont des std::string_view, donc OK a passer a spdlog::get
     * qui accepte un std::string (conversion implicite).
     */
    struct Loggers {
        static constexpr std::string_view boot        = "sea.boot";
        static constexpr std::string_view http        = "sea.http";
        static constexpr std::string_view application = "sea.application";
        static constexpr std::string_view persistence = "sea.persistence";
        static constexpr std::string_view security    = "sea.security";
        static constexpr std::string_view seastar     = "seastar";
    };

    /**
     * @return true si init() a deja ete appele (et pas shutdown).
     */
    [[nodiscard]] static bool is_initialized() noexcept;
    /**
     * Acces au sink ring buffer.
     *
     * Le sink est cree par init() et garde tous les logs en memoire
     * (10 000 entrees par defaut, ring buffer).
     *
     * Utilise par les handlers HTTP /admin/logs pour exposer les logs
     * via REST a SeaUI.
     *
     * @return shared_ptr au sink (nullptr si init() pas encore appele)
     */
    [[nodiscard]] static std::shared_ptr<RingBufferSink> get_ring_buffer_sink();


private:
    LoggingInitializer() = delete;   // classe utilitaire static
};

} // namespace sea::application::logging