#include "logging_initializer.h"
#include "ring_buffer_sink.h"
#include "spdlog/sinks/basic_file_sink.h"

#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/dist_sink.h>
#include <spdlog/pattern_formatter.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace sea::application::logging {

namespace {

namespace dlog = sea::domain::logging;

// ═════════════════════════════════════════════════════════════════════
// Helpers internes
// ═════════════════════════════════════════════════════════════════════

/**
 * Mapping LogLevel du domaine -> spdlog::level::level_enum
 */
[[nodiscard]] spdlog::level::level_enum to_spdlog_level(dlog::LogLevel level) noexcept
{
    switch (level) {
    case dlog::LogLevel::Trace:    return spdlog::level::trace;
    case dlog::LogLevel::Debug:    return spdlog::level::debug;
    case dlog::LogLevel::Info:     return spdlog::level::info;
    case dlog::LogLevel::Warn:     return spdlog::level::warn;
    case dlog::LogLevel::Error:    return spdlog::level::err;
    case dlog::LogLevel::Critical: return spdlog::level::critical;
    case dlog::LogLevel::Off:      return spdlog::level::off;
    }
    return spdlog::level::info;
}


// ─────────────────────────────────────────────────────────────────────
// Custom flag formatter : echappement JSON via nlohmann/json
//
// spdlog permet d'enregistrer des "flags" custom qui remplacent une
// lettre dans le pattern. On en cree un pour echapper proprement le
// message (et les autres champs textuels) en JSON.
//
// Flag utilise : %* (message JSON-escape — inclut les guillemets autour)
//
// Exemple :
//   message = Hello "world" with \n newline
//   sortie  = "Hello \"world\" with \n newline"   (inclut les " externes)
//
// On utilise nlohmann::json::dump() qui produit une chaine JSON valide
// avec tous les echappements RFC 8259 : ", \, \b, \f, \n, \r, \t,
// \uXXXX pour les chars de controle.
// ─────────────────────────────────────────────────────────────────────
class JsonEscapeMessageFlagFormatter : public spdlog::custom_flag_formatter {
public:
    void format(
        const spdlog::details::log_msg& msg,
        const std::tm&,
        spdlog::memory_buf_t& dest) override
    {
        // msg.payload est un string_view sur le message formatte.
        // On le passe a nlohmann::json qui produira un literal JSON
        // string (avec guillemets externes et echappements).
        const nlohmann::json j =
            std::string(msg.payload.data(), msg.payload.size());
        const std::string escaped = j.dump();

        // dump() retourne quelque chose comme : "Hello \"world\""
        // Comme on veut le guillemet externe dans le JSON output,
        // on copie tel quel (escaped contient deja les " autour).
        dest.append(escaped.data(), escaped.data() + escaped.size());
    }

    [[nodiscard]] std::unique_ptr<custom_flag_formatter> clone() const override {
        return spdlog::details::make_unique<JsonEscapeMessageFlagFormatter>();
    }
};

/**
 * Flag pour le logger name JSON-escape.
 *
 * Meme principe mais sur msg.logger_name. Generalement les noms de
 * loggers sont propres (sea.http, etc.) mais on echappe quand meme
 * par securite.
 */
class JsonEscapeLoggerNameFlagFormatter : public spdlog::custom_flag_formatter {
public:
    void format(
        const spdlog::details::log_msg& msg,
        const std::tm&,
        spdlog::memory_buf_t& dest) override
    {
        const nlohmann::json j =
            std::string(msg.logger_name.data(), msg.logger_name.size());
        const std::string escaped = j.dump();
        dest.append(escaped.data(), escaped.data() + escaped.size());
    }

    [[nodiscard]] std::unique_ptr<custom_flag_formatter> clone() const override {
        return spdlog::details::make_unique<JsonEscapeLoggerNameFlagFormatter>();
    }
};


/**
 * Construit un formatter spdlog selon le format souhaite.
 *
 * Text : pattern lisible classique
 *   [2026-05-13 14:32:01.234] [sea.http] [info] message
 *
 * Json : utilise les custom flags pour echapper proprement les champs
 *   {"timestamp":"...","logger":"sea.http","level":"info","message":"Hello \"world\""}
 *
 *   Flags custom utilises :
 *     %*  -> message echappe JSON (inclut les guillemets)
 *     %#  -> logger name echappe JSON (inclut les guillemets)
 *   Flags standards :
 *     %Y, %m, %d, %H, %M, %S, %e  -> composants timestamp
 *     %l                            -> niveau (trace/debug/info/...)
 *                                      Toujours alphanumerique, pas besoin d'echapper
 */
[[nodiscard]] std::unique_ptr<spdlog::pattern_formatter>
build_formatter(dlog::LogFormat format)
{
    auto formatter = std::make_unique<spdlog::pattern_formatter>();

    if (format == dlog::LogFormat::Json) {
        // Enregistre les flags custom %* et %#
        formatter->add_flag<JsonEscapeMessageFlagFormatter>('*');
        formatter->add_flag<JsonEscapeLoggerNameFlagFormatter>('#');

        // Pattern : tous les champs JSON. Les flags %* et %# incluent
        // deja les guillemets externes, donc PAS de " dans le pattern.
        formatter->set_pattern(
            R"({"timestamp":"%Y-%m-%dT%H:%M:%S.%eZ","logger":%#,"level":"%l","message":%*})"
            );
    } else {
        // Format texte classique
        formatter->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
    }

    return formatter;
}

/**
 * S'assure que le dossier parent d'un fichier de log existe.
 * Cree recursivement si necessaire.
 */
void ensure_parent_directory(const std::string& file_path)
{
    const std::filesystem::path p(file_path);
    const auto parent = p.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent)) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            throw std::runtime_error(
                "LoggingInitializer: failed to create log directory '" +
                parent.string() + "': " + ec.message()
                );
        }
    }
}

/**
 * Cree un sink spdlog selon la SinkConfig.
 *
 * Le pattern est applique au sink directement (et non au logger),
 * pour que chaque sink puisse avoir son propre format (console=text,
 * file=json par exemple).
 *
 * Strategie de rotation :
 * - Si max_size_bytes > 0 ET time_pattern == None : rotating_file_sink
 * - Si time_pattern != None ET max_size_bytes == 0 : daily_file_sink
 * - Si les deux : daily_file_sink (priorite au temps) + on accepte que
 *   le fichier puisse depasser max_size entre deux rotations daily
 *   (compromise — spdlog n'a pas de "size OR time" natif)
 * - Si aucun : basic_file_sink (pas de rotation, fichier grossit indefiniment)
 */
[[nodiscard]] spdlog::sink_ptr build_sink(const dlog::SinkConfig& cfg)
{
    spdlog::sink_ptr sink;

    if (cfg.type == dlog::SinkType::Console) {
        // Console color avec detection auto stdout/stderr
        sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    }
    else if (cfg.type == dlog::SinkType::File) {
        if (cfg.path.empty()) {
            throw std::runtime_error(
                "LoggingInitializer: sink type=file but path is empty"
                );
        }
        ensure_parent_directory(cfg.path);

        const auto& rot = cfg.rotation;

        if (rot.is_size_rotation_enabled() && !rot.is_time_rotation_enabled()) {
            // Rotation par taille uniquement
            sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                cfg.path,
                rot.max_size_bytes,
                rot.max_files
                );
        }
        else if (rot.is_time_rotation_enabled()) {
            // Rotation par temps (avec ou sans size).
            // daily_file_sink fait du daily a une heure precise.
            // Pour hourly, il faudrait soit un custom sink, soit un
            // rotating_file_sink avec max_size petit.
            //
            // MVP : daily fonctionne, hourly tombe en daily aussi
            // (a ameliorer si besoin via un custom sink).
            sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
                cfg.path,
                /* rotation hour */ 0,
                /* rotation minute */ 0,
                /* truncate */ false,
                /* max_files */ static_cast<uint16_t>(rot.max_files)
                );
        }
        else {
            // Aucune rotation : basic file sink
            sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
                cfg.path,
                /* truncate */ false
                );
        }
    }

    if (sink) {
        // Au lieu de set_pattern(string) qui ne gere pas l'echappement,
        // on utilise un formatter complet construit par build_formatter()
        // (qui pour le JSON utilise les custom flags %* et %# avec
        // echappement via nlohmann::json).
        sink->set_formatter(build_formatter(cfg.format));
    }
    return sink;
}

// ═════════════════════════════════════════════════════════════════════
// Etat global du module
// ═════════════════════════════════════════════════════════════════════

std::mutex            g_init_mutex;
bool                  g_initialized = false;
std::shared_ptr<RingBufferSink> g_ring_buffer_sink;
/**
 * Liste des noms de loggers a creer.
 * Synchronise avec LoggingInitializer::Loggers.
 */
const std::vector<std::string> kLoggerNames = {
    "sea.boot",
    "sea.http",
    "sea.application",
    "sea.persistence",
    "sea.runtime",
    "sea.security",
    "seastar",
};

/**
 * Cree un logger nomme partage entre shards, alimentant tous les sinks.
 *
 * @param name              Nom du logger
 * @param dist_sink         Sink "distributeur" qui contient tous les sinks reels
 * @param level             Niveau effectif pour ce logger
 * @param flush_level       Niveau au-dela duquel on flush immediatement
 * @param async             Si true, utilise spdlog::async_logger
 * @param overflow_policy   Politique de la queue async
 */
void register_logger(
    const std::string& name,
    spdlog::sink_ptr dist_sink,
    spdlog::level::level_enum level,
    spdlog::level::level_enum flush_level,
    bool async,
    spdlog::async_overflow_policy overflow_policy)
{
    // Si le logger existe deja (cas idempotent), on le supprime et on recree
    if (spdlog::get(name) != nullptr) {
        spdlog::drop(name);
    }

    std::shared_ptr<spdlog::logger> logger;

    if (async) {
        // Async logger : pousse dans la queue du thread pool global
        logger = std::make_shared<spdlog::async_logger>(
            name,
            dist_sink,
            spdlog::thread_pool(),
            overflow_policy
            );
    } else {
        // Logger synchrone classique
        logger = std::make_shared<spdlog::logger>(name, dist_sink);
    }

    logger->set_level(level);
    logger->flush_on(flush_level);

    spdlog::register_logger(logger);
}

} // namespace anonyme


// ═════════════════════════════════════════════════════════════════════
// LoggingInitializer — implementation
// ═════════════════════════════════════════════════════════════════════

void LoggingInitializer::init(const dlog::LoggingConfig& config)
{
    std::lock_guard<std::mutex> lock(g_init_mutex);

    // Validation cote domaine (sinks declares, paths non vides, etc.)
    config.validate();

    // Si logging desactive : drop tout et on met le niveau a off
    if (!config.is_enabled()) {
        spdlog::drop_all();
        spdlog::set_level(spdlog::level::off);
        g_initialized = false;
        return;
    }

    // ─── 1. Construire tous les sinks ───────────────────────────
    // Les sinks "enabled=false" sont sautés.
    // Le "dist_sink" est un sink qui ne fait que dispatcher vers
    // d'autres sinks — pratique pour appliquer tous les sinks
    // a tous nos loggers en une seule reference.
    auto dist_sink = std::make_shared<spdlog::sinks::dist_sink_mt>();
    for (const auto& sink_cfg : config.sinks()) {
        if (!sink_cfg.enabled) continue;
        try {
            auto sink = build_sink(sink_cfg);
            if (sink) {
                dist_sink->add_sink(sink);
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(
                std::string("LoggingInitializer::init: failed to build sink: ") + e.what()
                );
        }
    }


    // Sink ring buffer en memoire (10 000 entrees)
    // Toujours actif (independant du YAML) pour exposer /admin/logs
    g_ring_buffer_sink = std::make_shared<RingBufferSink>(10000);
    dist_sink->add_sink(g_ring_buffer_sink);

    if (dist_sink->sinks().empty()) {
        // Tous les sinks sont desactives -> rien a faire
        spdlog::drop_all();
        spdlog::shutdown();

        // Liberer le ring buffer
        g_ring_buffer_sink.reset();

        g_initialized = false;

    }

    // ─── 2. Initialiser le thread pool async (si applicable) ────
    const auto& async_cfg = config.async_config();

    spdlog::async_overflow_policy overflow_policy =
        spdlog::async_overflow_policy::overrun_oldest;
    if (async_cfg.overflow_policy == dlog::AsyncConfig::OverflowPolicy::Block) {
        overflow_policy = spdlog::async_overflow_policy::block;
    }

    if (async_cfg.enabled) {
        // 1 thread dedie pour ecrire dans les sinks.
        // Tous les async_logger partagent ce thread pool.
        // Note: si init() est appele plusieurs fois, on remplace le pool.
        spdlog::init_thread_pool(async_cfg.queue_size, /* n_threads */ 1);
    }

    // ─── 3. Niveaux ─────────────────────────────────────────────
    const auto global_level      = to_spdlog_level(config.level());
    const auto global_flush_level = to_spdlog_level(config.flush_level());

    // ─── 4. Creer les loggers nommes ────────────────────────────
    for (const auto& name : kLoggerNames) {
        const auto module_level = to_spdlog_level(
            config.effective_level_for(name)
            );

        register_logger(
            name,
            dist_sink,
            module_level,
            global_flush_level,
            async_cfg.enabled,
            overflow_policy
            );
    }

    // ─── 5. Configurer le default_logger (tolerant) ─────────────
    // spdlog::info(...) ira sur default_logger, qui partage les memes
    // sinks et le niveau global.
    register_logger(
        "sea.default",
        dist_sink,
        global_level,
        global_flush_level,
        async_cfg.enabled,
        overflow_policy
        );
    spdlog::set_default_logger(spdlog::get("sea.default"));

    // ─── 6. Hop ────────────────────────────────────────────────
    g_initialized = true;

    // Log de confirmation via le logger boot (si declare a debug ou plus bas)
    auto boot = spdlog::get("sea.boot");
    if (boot && boot->should_log(spdlog::level::info)) {
        boot->info("Logging initialized: level={} sinks={} async={} queue_size={}",
                   to_string(config.level()),
                   dist_sink->sinks().size(),
                   async_cfg.enabled ? "yes" : "no",
                   async_cfg.queue_size);
    }
}

void LoggingInitializer::shutdown()
{
    std::lock_guard<std::mutex> lock(g_init_mutex);

    if (!g_initialized) return;

    // Flush tous les loggers AVANT de les drop
    // (sinon les messages en queue async sont perdus)
    auto flush_one = [](const std::shared_ptr<spdlog::logger>& lg) {
        if (lg) lg->flush();
    };

    for (const auto& name : kLoggerNames) {
        flush_one(spdlog::get(name));
    }
    flush_one(spdlog::default_logger());

    // Drop tous les loggers et shutdown du thread pool
    spdlog::drop_all();
    spdlog::shutdown();

    g_initialized = false;
}

bool LoggingInitializer::is_initialized() noexcept
{
    std::lock_guard<std::mutex> lock(g_init_mutex);
    return g_initialized;
}
std::shared_ptr<RingBufferSink> LoggingInitializer::get_ring_buffer_sink()
{
    std::lock_guard<std::mutex> lock(g_init_mutex);
    return g_ring_buffer_sink;
}

} // namespace sea::application::logging