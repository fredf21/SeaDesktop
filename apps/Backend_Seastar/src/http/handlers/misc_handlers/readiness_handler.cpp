#include "readiness_handler.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <seastar/core/future.hh>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <utility>

namespace sea::http::handlers::misc {

using json = nlohmann::json;

namespace {

/**
 * Format ISO 8601 UTC : "2026-06-13T15:32:00.123Z"
 */
[[nodiscard]] std::string format_timestamp_now()
{
    const auto now = std::chrono::system_clock::now();
    const auto time_t_value = std::chrono::system_clock::to_time_t(now);
    const auto ms_since_epoch =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
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

} // namespace anonyme


ReadinessHandler::ReadinessHandler(
    std::shared_ptr<sea::infrastructure::persistence::IGenericRepository> repository,
    std::shared_ptr<sea::infrastructure::storage::IFileStorage> storage)
    : repository_(std::move(repository))
    , storage_(std::move(storage))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
ReadinessHandler::handle(const seastar::sstring&,
                         std::unique_ptr<seastar::http::request>,
                         std::unique_ptr<seastar::http::reply> rep)
{
    json checks = json::object();
    bool all_ok = true;

    // ─── Check 1 : Base de donnees ──────────────────────────────
    // On execute une transaction no-op : BEGIN; COMMIT; sans operation
    // metier. Si la connexion est cassee, in_transaction throw ou
    // tx_result.committed sera false.
    //
    // Pourquoi pas un simple count() ? Parce que count() depend d'une
    // table specifique. Un test de transaction valide la connexion
    // sans dependre du schema utilisateur.
    if (!repository_) {
        checks["database"] = "error: repository not initialized";
        all_ok = false;
    } else {
        try {
            const auto tx_result = co_await repository_->in_transaction(
                []() -> seastar::future<bool> {
                    co_return true;   // commit immediat
                }
                );
            if (tx_result.committed) {
                checks["database"] = "ok";
            } else {
                checks["database"] = "error: " +
                                     (tx_result.error_message.empty()
                                          ? std::string("transaction not committed")
                                          : tx_result.error_message);
                all_ok = false;
            }
        } catch (const std::exception& e) {
            checks["database"] = std::string("error: ") + e.what();
            all_ok = false;
        } catch (...) {
            checks["database"] = "error: unknown exception";
            all_ok = false;
        }
    }

    // ─── Check 2 : Storage de fichiers (optionnel) ──────────────
    // Si le storage n'est pas configure (service sans gestion de
    // fichiers), on ne le check pas et il n'apparait pas dans la
    // reponse. Si il est configure, on appelle exists() sur un path
    // arbitraire : on attend false (pas trouve) ou une StorageException
    // si le storage est en panne.
    if (storage_) {
        try {
            // Le path n'a pas besoin d'exister. exists() retourne false
            // si le path est OK mais le fichier absent. Une exception
            // signale un probleme reel (root inaccessible, permission,
            // etc.).
            (void) storage_->exists(".health_check");
            checks["storage"] = "ok";
        } catch (const std::exception& e) {
            checks["storage"] = std::string("error: ") + e.what();
            all_ok = false;
        } catch (...) {
            checks["storage"] = "error: unknown exception";
            all_ok = false;
        }
    }

    // ─── Construction de la reponse ─────────────────────────────
    json response;
    response["status"] = all_ok ? "ready" : "not_ready";
    response["checks"] = std::move(checks);
    response["timestamp"] = format_timestamp_now();

    if (!all_ok) {
        // 503 Service Unavailable : standard pour readiness probe
        // echouee. Les load balancers et orchestrateurs (k8s) savent
        // l'interpreter et retirent le pod du pool jusqu'au prochain
        // check passant.
        rep->set_status(seastar::http::reply::status_type::service_unavailable);
        if (auto log = spdlog::get("sea.http")) {
            log->warn("ReadinessHandler: not ready - {}",
                      response["checks"].dump());
        }
    } else {
        rep->set_status(seastar::http::reply::status_type::ok);
    }

    rep->write_body("application/json", response.dump());
    co_return std::move(rep);
}

} // namespace sea::http::handlers::misc