#include "logging_config.h"

#include <cctype>
#include <stdexcept>
#include <utility>

namespace sea::domain::logging {

namespace {

[[nodiscard]] std::string to_lower(std::string_view sv)
{
    std::string out;
    out.reserve(sv.size());
    for (char c : sv) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

} // namespace anonyme


// ═════════════════════════════════════════════════════════════════════
// LogLevel
// ═════════════════════════════════════════════════════════════════════

LogLevel log_level_from_string(std::string_view s)
{
    const auto lower = to_lower(s);
    if (lower == "trace")    return LogLevel::Trace;
    if (lower == "debug")    return LogLevel::Debug;
    if (lower == "info")     return LogLevel::Info;
    if (lower == "warn" || lower == "warning") return LogLevel::Warn;
    if (lower == "error" || lower == "err")    return LogLevel::Error;
    if (lower == "critical" || lower == "crit") return LogLevel::Critical;
    if (lower == "off" || lower == "none")     return LogLevel::Off;

    throw std::invalid_argument(
        "LoggingConfig: unknown level '" + std::string(s) +
        "' (expected: trace | debug | info | warn | error | critical | off)"
        );
}

std::string_view to_string(LogLevel level) noexcept
{
    switch (level) {
    case LogLevel::Trace:    return "trace";
    case LogLevel::Debug:    return "debug";
    case LogLevel::Info:     return "info";
    case LogLevel::Warn:     return "warn";
    case LogLevel::Error:    return "error";
    case LogLevel::Critical: return "critical";
    case LogLevel::Off:      return "off";
    }
    return "unknown";
}


// ═════════════════════════════════════════════════════════════════════
// LogFormat
// ═════════════════════════════════════════════════════════════════════

LogFormat log_format_from_string(std::string_view s)
{
    const auto lower = to_lower(s);
    if (lower == "text" || lower == "plain") return LogFormat::Text;
    if (lower == "json")                     return LogFormat::Json;
    throw std::invalid_argument(
        "LoggingConfig: unknown format '" + std::string(s) +
        "' (expected: text | json)"
        );
}

std::string_view to_string(LogFormat format) noexcept
{
    switch (format) {
    case LogFormat::Text: return "text";
    case LogFormat::Json: return "json";
    }
    return "unknown";
}


// ═════════════════════════════════════════════════════════════════════
// SinkType
// ═════════════════════════════════════════════════════════════════════

SinkType sink_type_from_string(std::string_view s)
{
    const auto lower = to_lower(s);
    if (lower == "console" || lower == "stderr" || lower == "stdout") {
        return SinkType::Console;
    }
    if (lower == "file") return SinkType::File;
    throw std::invalid_argument(
        "LoggingConfig: unknown sink type '" + std::string(s) +
        "' (expected: console | file)"
        );
}

std::string_view to_string(SinkType type) noexcept
{
    switch (type) {
    case SinkType::Console: return "console";
    case SinkType::File:    return "file";
    }
    return "unknown";
}


// ═════════════════════════════════════════════════════════════════════
// TimePattern
// ═════════════════════════════════════════════════════════════════════

TimePattern time_pattern_from_string(std::string_view s)
{
    const auto lower = to_lower(s);
    if (lower == "none")   return TimePattern::None;
    if (lower == "hourly") return TimePattern::Hourly;
    if (lower == "daily")  return TimePattern::Daily;
    throw std::invalid_argument(
        "LoggingConfig: unknown time_pattern '" + std::string(s) +
        "' (expected: none | hourly | daily)"
        );
}

std::string_view to_string(TimePattern pattern) noexcept
{
    switch (pattern) {
    case TimePattern::None:   return "none";
    case TimePattern::Hourly: return "hourly";
    case TimePattern::Daily:  return "daily";
    }
    return "unknown";
}


// ═════════════════════════════════════════════════════════════════════
// LoggingConfig
// ═════════════════════════════════════════════════════════════════════

// ─── Factory ─────────────────────────────────────────────────────────

LoggingConfig LoggingConfig::safe_defaults()
{
    LoggingConfig cfg;
    cfg.enabled_     = true;
    cfg.level_       = LogLevel::Info;
    cfg.flush_level_ = LogLevel::Error;

    // Un seul sink par defaut : console texte
    SinkConfig console_sink;
    console_sink.type    = SinkType::Console;
    console_sink.format  = LogFormat::Text;
    console_sink.enabled = true;
    cfg.sinks_.push_back(std::move(console_sink));

    // Async par defaut (ne pas bloquer le reactor)
    cfg.async_.enabled         = true;
    cfg.async_.queue_size      = 8192;
    cfg.async_.overflow_policy = AsyncConfig::OverflowPolicy::OverrunOldest;

    return cfg;
}

LoggingConfig LoggingConfig::disabled()
{
    LoggingConfig cfg;
    cfg.enabled_ = false;
    cfg.level_   = LogLevel::Off;
    return cfg;
}

// ─── Setters ─────────────────────────────────────────────────────────

LoggingConfig& LoggingConfig::set_level(LogLevel level)
{
    level_ = level;
    return *this;
}

LoggingConfig& LoggingConfig::set_module_level(std::string module_name, LogLevel level)
{
    module_levels_[std::move(module_name)] = level;
    return *this;
}

LoggingConfig& LoggingConfig::add_sink(SinkConfig sink)
{
    sinks_.push_back(std::move(sink));
    return *this;
}

LoggingConfig& LoggingConfig::set_sinks(std::vector<SinkConfig> sinks)
{
    sinks_ = std::move(sinks);
    return *this;
}

LoggingConfig& LoggingConfig::set_flush_level(LogLevel level)
{
    flush_level_ = level;
    return *this;
}

LoggingConfig& LoggingConfig::set_async(AsyncConfig cfg)
{
    async_ = std::move(cfg);
    return *this;
}

LoggingConfig& LoggingConfig::set_enabled(bool enabled)
{
    enabled_ = enabled;
    return *this;
}

// ─── Accesseur calcule ───────────────────────────────────────────────

LogLevel LoggingConfig::effective_level_for(const std::string& module_name) const
{
    // Recherche exacte
    const auto it = module_levels_.find(module_name);
    if (it != module_levels_.end()) {
        return it->second;
    }

    // Recherche par prefixe : "sea.http.login_handler" matche "sea.http"
    // (utile si tu utilises des loggers fins)
    // Choisit le prefixe le plus long qui matche.
    std::string best_prefix;
    LogLevel    best_level = level_;
    for (const auto& [name, lvl] : module_levels_) {
        if (module_name.size() >= name.size() + 1 &&
            module_name.compare(0, name.size(), name) == 0 &&
            module_name[name.size()] == '.') {
            // Prefixe match
            if (name.size() > best_prefix.size()) {
                best_prefix = name;
                best_level  = lvl;
            }
        }
    }

    return best_level;
}

// ─── Validation ──────────────────────────────────────────────────────

void LoggingConfig::validate() const
{
    if (!enabled_) {
        return;   // rien a valider si desactive
    }

    if (sinks_.empty()) {
        throw std::invalid_argument(
            "LoggingConfig: no sinks declared (need at least one when enabled)"
            );
    }

    bool any_enabled = false;
    for (std::size_t i = 0; i < sinks_.size(); ++i) {
        const auto& sink = sinks_[i];
        if (!sink.enabled) continue;
        any_enabled = true;

        if (sink.type == SinkType::File) {
            if (sink.path.empty()) {
                throw std::invalid_argument(
                    "LoggingConfig: sink #" + std::to_string(i) +
                    " is type=file but path is empty"
                    );
            }
            if (sink.rotation.max_files == 0) {
                throw std::invalid_argument(
                    "LoggingConfig: sink #" + std::to_string(i) +
                    " has rotation.max_files == 0 (must be >= 1)"
                    );
            }
        }
    }

    if (!any_enabled) {
        throw std::invalid_argument(
            "LoggingConfig: all sinks are disabled (logging will produce no output)"
            );
    }

    if (async_.enabled && async_.queue_size == 0) {
        throw std::invalid_argument(
            "LoggingConfig: async.queue_size must be > 0 when async is enabled"
            );
    }
}

} // namespace sea::domain::logging