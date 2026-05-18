#pragma once

#include <chrono>
#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace sea::domain::logging {

// ─────────────────────────────────────────────────────────────────────
// Niveau de log
//
// Aligne sur spdlog::level::level_enum (sans dependance directe a spdlog
// au niveau du domaine).
// ─────────────────────────────────────────────────────────────────────
enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Critical,
    Off
};

[[nodiscard]] LogLevel log_level_from_string(std::string_view s);
[[nodiscard]] std::string_view to_string(LogLevel level) noexcept;


// ─────────────────────────────────────────────────────────────────────
// Format de sortie d'un sink
//
// Text : ligne lisible
//   [2026-05-13 14:32:01.234] [sea.http] [info] login successful: alice@ex.com
//
// Json : ligne JSON pour ingestion Loki/ELK
//   {"timestamp":"2026-05-13T14:32:01.234Z","logger":"sea.http","level":"info",
//    "message":"login successful: alice@ex.com"}
// ─────────────────────────────────────────────────────────────────────
enum class LogFormat {
    Text,
    Json
};

[[nodiscard]] LogFormat log_format_from_string(std::string_view s);
[[nodiscard]] std::string_view to_string(LogFormat format) noexcept;


// ─────────────────────────────────────────────────────────────────────
// Type de sink
//
// Console : stderr/stdout, pas de rotation
// File    : fichier sur disque, rotation possible
// ─────────────────────────────────────────────────────────────────────
enum class SinkType {
    Console,
    File
};

[[nodiscard]] SinkType sink_type_from_string(std::string_view s);
[[nodiscard]] std::string_view to_string(SinkType type) noexcept;


// ─────────────────────────────────────────────────────────────────────
// Pattern de rotation par temps
//
// None   : pas de rotation temporelle (seule la taille peut declencher)
// Hourly : nouveau fichier chaque heure
// Daily  : nouveau fichier chaque jour (a minuit)
// ─────────────────────────────────────────────────────────────────────
enum class TimePattern {
    None,
    Hourly,
    Daily
};

[[nodiscard]] TimePattern time_pattern_from_string(std::string_view s);
[[nodiscard]] std::string_view to_string(TimePattern pattern) noexcept;


// ─────────────────────────────────────────────────────────────────────
// Configuration de rotation pour un sink de type File
//
// La rotation peut etre declenchee par taille, par temps, ou les deux
// (premier qui declenche). Si max_size_bytes == 0 et time_pattern == None,
// pas de rotation du tout.
// ─────────────────────────────────────────────────────────────────────
struct RotationConfig {
    // Rotation par taille (0 = desactivee)
    std::size_t max_size_bytes = 100 * 1024 * 1024;   // 100 MB

    // Rotation par temps
    TimePattern time_pattern = TimePattern::Daily;

    // Nombre d'archives a garder (les plus anciennes sont supprimees)
    std::size_t max_files = 10;

    // Compresser les archives (.gz)
    // Note: spdlog ne supporte pas la compression native. Si true, un hook
    // post-rotation sera utilise (a implementer si necessaire).
    bool compress = false;

    [[nodiscard]] bool is_size_rotation_enabled() const noexcept {
        return max_size_bytes > 0;
    }
    [[nodiscard]] bool is_time_rotation_enabled() const noexcept {
        return time_pattern != TimePattern::None;
    }
};


// ─────────────────────────────────────────────────────────────────────
// Configuration d'un sink (sortie de logs)
//
// Un sink = une destination. On peut en avoir plusieurs (console + file).
// Tous les loggers ecrivent dans tous les sinks (filtres par niveau).
// ─────────────────────────────────────────────────────────────────────
struct SinkConfig {
    SinkType    type    = SinkType::Console;
    LogFormat   format  = LogFormat::Text;
    bool        enabled = true;

    // ── Specifique aux sinks File ──
    std::string path;                // ex: "./logs/service.log"
    RotationConfig rotation;
};


// ─────────────────────────────────────────────────────────────────────
// Configuration async logging
//
// Si enabled, les ecritures sont decharges dans un thread dedie pour
// ne pas bloquer le reactor Seastar (spdlog::async_logger).
//
// queue_size : taille du buffer en messages. Si plein, le comportement
// depend de overflow_policy.
// ─────────────────────────────────────────────────────────────────────
struct AsyncConfig {
    bool        enabled    = true;
    std::size_t queue_size = 8192;

    // Politique en cas de queue pleine :
    // - block        : le caller attend qu'il y ait de la place (bloquant)
    // - overrun_oldest : ecrase les plus vieux messages (recommande pour reactor)
    enum class OverflowPolicy {
        Block,
        OverrunOldest
    };
    OverflowPolicy overflow_policy = OverflowPolicy::OverrunOldest;
};


// ─────────────────────────────────────────────────────────────────────
// LoggingConfig — configuration complete du logging d'un service
//
// Structure YAML :
//
//   logging:
//     level: info
//     modules:
//       sea.http: debug
//       sea.persistence: info
//       seastar: warn
//     sinks:
//       - type: console
//         format: text
//         enabled: true
//       - type: file
//         format: json
//         enabled: true
//         path: "./logs/service.log"
//         rotation:
//           max_size: "100MB"
//           time_pattern: daily
//           max_files: 10
//           compress: false
//     flush_level: error
//     async:
//       enabled: true
//       queue_size: 8192
//       overflow_policy: overrun_oldest
// ─────────────────────────────────────────────────────────────────────
class LoggingConfig {
public:
    // ─── Constructeurs / Factory ─────────────────────────────────
    LoggingConfig() = default;

    /// Defaults sensibles pour la production
    [[nodiscard]] static LoggingConfig safe_defaults();

    /// Tout desactive (mode "silencieux")
    [[nodiscard]] static LoggingConfig disabled();

    // ─── Builder fluide ──────────────────────────────────────────
    LoggingConfig& set_level(LogLevel level);
    LoggingConfig& set_module_level(std::string module_name, LogLevel level);
    LoggingConfig& add_sink(SinkConfig sink);
    LoggingConfig& set_sinks(std::vector<SinkConfig> sinks);
    LoggingConfig& set_flush_level(LogLevel level);
    LoggingConfig& set_async(AsyncConfig cfg);
    LoggingConfig& set_enabled(bool enabled);

    // ─── Accesseurs ──────────────────────────────────────────────
    [[nodiscard]] bool                                       is_enabled()      const noexcept { return enabled_; }
    [[nodiscard]] LogLevel                                   level()           const noexcept { return level_; }
    [[nodiscard]] const std::map<std::string, LogLevel>&    module_levels()   const noexcept { return module_levels_; }
    [[nodiscard]] const std::vector<SinkConfig>&            sinks()           const noexcept { return sinks_; }
    [[nodiscard]] LogLevel                                   flush_level()     const noexcept { return flush_level_; }
    [[nodiscard]] const AsyncConfig&                         async_config()    const noexcept { return async_; }

    /**
     * Retourne le niveau effectif pour un module nomme.
     *
     * Si le module a un override declare dans modules:, retourne celui-ci.
     * Sinon retourne le niveau global.
     */
    [[nodiscard]] LogLevel effective_level_for(const std::string& module_name) const;

    // ─── Validation ──────────────────────────────────────────────
    /**
     * @throws std::invalid_argument si :
     * - aucun sink declare (et logging active)
     * - sink File sans path
     * - rotation.max_files == 0
     */
    void validate() const;

private:
    bool                                  enabled_      = true;
    LogLevel                              level_        = LogLevel::Info;
    std::map<std::string, LogLevel>       module_levels_;
    std::vector<SinkConfig>               sinks_;
    LogLevel                              flush_level_  = LogLevel::Error;
    AsyncConfig                           async_;
};

} // namespace sea::domain::logging