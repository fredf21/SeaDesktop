#include "logs_handler.h"

#include "ring_buffer_sink.h"
#include "logging_initializer.h"

#include <seastar/util/log.hh>
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace sea::http::handlers::logs::admin {

using json = nlohmann::json;
namespace logging_ns = sea::application::logging;

namespace {

// ─────────────────────────────────────────────────────────────────────
// Helpers de conversion
// ─────────────────────────────────────────────────────────────────────

[[nodiscard]] std::string level_to_string(spdlog::level::level_enum lvl)
{
    switch (lvl) {
    case spdlog::level::trace:    return "trace";
    case spdlog::level::debug:    return "debug";
    case spdlog::level::info:     return "info";
    case spdlog::level::warn:     return "warn";
    case spdlog::level::err:      return "error";
    case spdlog::level::critical: return "critical";
    case spdlog::level::off:      return "off";
    default:                       return "unknown";
    }
}

[[nodiscard]] std::optional<spdlog::level::level_enum>
parse_level(const std::string& s)
{
    if (s == "trace")    return spdlog::level::trace;
    if (s == "debug")    return spdlog::level::debug;
    if (s == "info")     return spdlog::level::info;
    if (s == "warn" || s == "warning") return spdlog::level::warn;
    if (s == "error" || s == "err")    return spdlog::level::err;
    if (s == "critical" || s == "crit") return spdlog::level::critical;
    return std::nullopt;
}

/**
 * Format ISO 8601 UTC avec millisecondes : "2026-05-14T10:23:45.123Z"
 */
[[nodiscard]] std::string format_timestamp(
    const std::chrono::system_clock::time_point& tp)
{
    const auto time_t_value = std::chrono::system_clock::to_time_t(tp);
    const auto ms_since_epoch =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            tp.time_since_epoch()
            ).count();
    const auto ms_only = ms_since_epoch % 1000;

    std::tm tm_utc{};
    gmtime_r(&time_t_value, &tm_utc);

    std::ostringstream oss;
    oss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms_only
        << 'Z';
    return oss.str();
}

/**
 * Recupere un query param Seastar, retourne nullopt si absent.
 */
[[nodiscard]] std::optional<std::string>
get_query_param(const seastar::http::request& req, const std::string& name)
{
    const auto value = req.get_query_param(name);
    if (value.empty()) return std::nullopt;
    return std::string(value.data(), value.size());
}

[[nodiscard]] std::optional<std::uint64_t>
parse_uint64(const std::optional<std::string>& s)
{
    if (!s.has_value() || s->empty()) return std::nullopt;
    try {
        return std::stoull(*s);
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<std::size_t>
parse_size_t(const std::optional<std::string>& s)
{
    if (!s.has_value() || s->empty()) return std::nullopt;
    try {
        return static_cast<std::size_t>(std::stoull(*s));
    } catch (...) {
        return std::nullopt;
    }
}

/**
 * Garde d'acces admin : verifie que X-User-Role correspond au role admin
 * configure (passe en parametre depuis service.access_control.admin_role()).
 *
 * Le header X-User-Role est injecte en amont par ProtectedHandler apres
 * verification du JWT. Si ce header est absent, le user n'est pas
 * authentifie (ne devrait pas arriver puisque la route est wrappee par
 * ProtectedHandler avec requires_auth=true, mais on garde la check
 * defensive).
 *
 * @return Reponse 401/403 deja remplie si refus, nullptr si OK.
 *         Si retour non-nullptr, l'appelant doit co_return std::move(rep).
 */
[[nodiscard]] std::unique_ptr<seastar::http::reply>
check_admin_role(
    const seastar::http::request& req,
    const std::string& expected_admin_role,
    std::unique_ptr<seastar::http::reply> rep)
{
    const auto role_it = req._headers.find("X-User-Role");
    if (role_it == req._headers.end() || role_it->second.empty()) {
        rep->set_status(seastar::http::reply::status_type::unauthorized);
        rep->write_body("application/json",
                        json{{"error", "Authentication required"}}.dump());
        return rep;
    }

    // Comparaison stricte sur le nom configure dans le YAML
    // (authorization.admin_role, default "admin").
    const std::string_view role_view(
        role_it->second.data(), role_it->second.size()
        );
    if (role_view != expected_admin_role) {
        rep->set_status(seastar::http::reply::status_type::forbidden);
        rep->write_body("application/json",
                        json{{"error", "Admin role required"},
                             {"required_role", expected_admin_role}}.dump());
        return rep;
    }

    return nullptr;   // OK, garde passee
}

} // namespace anonyme


// ═════════════════════════════════════════════════════════════════════
// LogsHandler — GET /admin/logs
// ═════════════════════════════════════════════════════════════════════

LogsHandler::LogsHandler(
    std::shared_ptr<logging_ns::RingBufferSink> sink,
    std::string admin_role)
    : sink_(std::move(sink))
    , admin_role_(std::move(admin_role))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
LogsHandler::handle(const seastar::sstring&,
                    std::unique_ptr<seastar::http::request> req,
                    std::unique_ptr<seastar::http::reply> rep)
{
    // ─── 0. Garde admin role (configurable via YAML) ──────────────
    auto guard_response = check_admin_role(*req, admin_role_, std::move(rep));
    if (guard_response) {
        co_return std::move(guard_response);
    }
    // recreer le reply (move l'a invalide)
    rep = std::make_unique<seastar::http::reply>();

    if (!sink_) {
        rep->set_status(seastar::http::reply::status_type::service_unavailable);
        rep->write_body("application/json",
                        json{{"error", "Logging ring buffer not initialized"}}.dump());
        co_return std::move(rep);
    }

    // ─── Parse query params ───────────────────────────────────────
    logging_ns::RingBufferSink::Query q;

    // limit (default 100, max 1000)
    {
        const auto v = parse_size_t(get_query_param(*req, "limit"));
        if (v.has_value()) {
            q.limit = std::min(*v, static_cast<std::size_t>(1000));
        }
    }

    // level
    {
        const auto v = get_query_param(*req, "level");
        if (v.has_value()) {
            const auto lvl = parse_level(*v);
            if (!lvl.has_value()) {
                rep->set_status(seastar::http::reply::status_type::bad_request);
                rep->write_body("application/json",
                                json{{"error", "Invalid 'level' parameter"},
                                    {"value", *v},
                                    {"accepted",
                                     {"trace", "debug", "info", "warn", "error", "critical"}}
                                }.dump());
                co_return std::move(rep);
            }
            q.min_level = *lvl;
        }
    }

    // logger
    {
        const auto v = get_query_param(*req, "logger");
        if (v.has_value()) {
            q.logger_name = *v;
        }
    }

    // since (sequence_id)
    {
        const auto v = parse_uint64(get_query_param(*req, "since"));
        if (v.has_value()) {
            q.since_sequence = *v;
        }
    }

    // search
    {
        const auto v = get_query_param(*req, "search");
        if (v.has_value()) {
            q.search = *v;
        }
    }

    // ─── Lecture du buffer ────────────────────────────────────────
    const auto entries = sink_->query(q);

    // ─── Construction de la reponse JSON ─────────────────────────
    json response;
    response["logs"] = json::array();
    for (const auto& entry : entries) {
        response["logs"].push_back({
            {"sequence_id", entry.sequence_id},
            {"timestamp",   format_timestamp(entry.timestamp)},
            {"logger",      entry.logger_name},
            {"level",       level_to_string(entry.level)},
            {"message",     entry.message}
        });
    }

    response["count"]            = entries.size();
    response["next_sequence_id"] = sink_->next_sequence_id();
    response["buffer_size"]      = sink_->size();
    response["buffer_capacity"]  = sink_->capacity();

    rep->set_status(seastar::http::reply::status_type::ok);
    rep->write_body("application/json", response.dump());
    co_return std::move(rep);
}


// ═════════════════════════════════════════════════════════════════════
// LoggersListHandler — GET /admin/logs/loggers
// ═════════════════════════════════════════════════════════════════════

LoggersListHandler::LoggersListHandler(std::string admin_role)
    : admin_role_(std::move(admin_role))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
LoggersListHandler::handle(const seastar::sstring&,
                           std::unique_ptr<seastar::http::request> req,
                           std::unique_ptr<seastar::http::reply> rep)
{
    // ─── Garde admin role ────────────────────────────────────────
    auto guard_response = check_admin_role(*req, admin_role_, std::move(rep));
    if (guard_response) {
        co_return std::move(guard_response);
    }
    rep = std::make_unique<seastar::http::reply>();

    using Loggers = logging_ns::LoggingInitializer::Loggers;

    json response;
    response["loggers"] = json::array({
        std::string(Loggers::boot),
        std::string(Loggers::http),
        std::string(Loggers::application),
        std::string(Loggers::persistence),
        std::string(Loggers::runtime),
        std::string(Loggers::security),
        std::string(Loggers::seastar)
    });

    rep->set_status(seastar::http::reply::status_type::ok);
    rep->write_body("application/json", response.dump());
    co_return std::move(rep);
}

} // namespace sea::http::handlers::logs::admin