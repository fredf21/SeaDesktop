#include "list_projects_handler.h"

#include "../../errors/error_response_factory.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <filesystem>
#include <utility>

namespace sea::http::handlers::admin {

using json = nlohmann::json;
namespace errors = sea::http::errors;
namespace fs = std::filesystem;
using Status = seastar::http::reply::status_type;

namespace {

/**
 * Verifie que le user a le role admin via le header X-User-Role
 * injecte par ProtectedHandler. Retourne nullptr si OK, sinon une
 * reply pre-remplie (401/403) que l'appelant doit retourner.
 */
[[nodiscard]] std::unique_ptr<seastar::http::reply>
check_admin_role(const seastar::http::request& req,
                 const std::string& expected_admin_role)
{
    const auto role_it = req._headers.find("X-User-Role");
    if (role_it == req._headers.end() || role_it->second.empty()) {
        return errors::make_error_reply(
            Status::unauthorized, "AUTHENTICATION_ERROR",
            "Authentication required.");
    }

    const std::string_view role_view(
        role_it->second.data(), role_it->second.size()
        );
    if (role_view != expected_admin_role) {
        return errors::make_error_reply(
            Status::forbidden, "AUTHORIZATION_ERROR",
            "Admin role required.");
    }
    return nullptr;
}

/**
 * Verifie qu'une extension de fichier est un YAML (.yaml ou .yml,
 * case-insensitive).
 */
[[nodiscard]] bool is_yaml_extension(const std::string& ext)
{
    std::string lower;
    lower.reserve(ext.size());
    for (char c : ext) {
        lower.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    }
    return lower == ".yaml" || lower == ".yml";
}

} // namespace anonyme


ListProjectsHandler::ListProjectsHandler(std::string configs_dir,
                                         std::string admin_role)
    : configs_dir_(std::move(configs_dir))
    , admin_role_(std::move(admin_role))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
ListProjectsHandler::handle(const seastar::sstring&,
                            std::unique_ptr<seastar::http::request> req,
                            std::unique_ptr<seastar::http::reply> rep)
{
    // ─── 1. Garde role admin ─────────────────────────────────────
    if (auto guard = check_admin_role(*req, admin_role_); guard) {
        co_return std::move(guard);
    }

    // ─── 2. Verification du dossier configs ──────────────────────
    std::error_code ec;
    const auto configs_path = fs::path(configs_dir_);
    if (!fs::exists(configs_path, ec) || !fs::is_directory(configs_path, ec)) {
        // Cas suspect : configs_dir absent ou pas un dossier. On
        // logue pour diagnostic mais on retourne une reponse vide
        // plutot qu'une 500 -- un dossier vide est un etat valide.
        if (auto log = spdlog::get("sea.http")) {
            log->warn(
                "ListProjectsHandler: configs_dir does not exist or is "
                "not a directory: '{}'",
                configs_dir_);
        }
        json response;
        response["projects"] = json::array();
        rep->set_status(Status::ok);
        rep->write_body("application/json", response.dump());
        co_return std::move(rep);
    }

    // ─── 3. Enumeration des fichiers YAML ────────────────────────
    std::vector<std::pair<std::string, std::string>> projects;
    // pair = (name sans extension, nom de fichier complet)

    try {
        for (const auto& entry : fs::directory_iterator(configs_path)) {
            if (!entry.is_regular_file()) continue;

            const auto& filename = entry.path().filename().string();
            const auto  ext      = entry.path().extension().string();

            if (!is_yaml_extension(ext)) continue;

            // Skip les fichiers caches (commencent par '.') et les
            // backups (.bak, .backup, etc.) au cas ou.
            if (!filename.empty() && filename[0] == '.') continue;

            const auto name = entry.path().stem().string();
            projects.emplace_back(name, filename);
        }
    } catch (const std::exception& e) {
        if (auto log = spdlog::get("sea.http")) {
            log->error(
                "ListProjectsHandler: error reading configs_dir '{}': {}",
                configs_dir_, e.what());
        }
        co_return errors::make_internal_error_reply();
    }

    // Tri alphabetique par nom pour une reponse deterministe.
    std::sort(projects.begin(), projects.end(),
              [](const auto& a, const auto& b) {
                  return a.first < b.first;
              });

    // ─── 4. Construction de la reponse JSON ──────────────────────
    json response;
    response["projects"] = json::array();
    for (const auto& [name, file] : projects) {
        response["projects"].push_back({
            {"name", name},
            {"file", file}
        });
    }

    rep->set_status(Status::ok);
    rep->write_body("application/json", response.dump());

    if (auto log = spdlog::get("sea.application")) {
        log->debug(
            "ListProjectsHandler: returned {} project(s) from '{}'",
            projects.size(), configs_dir_);
    }

    co_return std::move(rep);
}

} // namespace sea::http::handlers::admin