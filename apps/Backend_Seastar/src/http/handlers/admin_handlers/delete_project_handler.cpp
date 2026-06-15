#include "delete_project_handler.h"

#include "../../errors/error_response_factory.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <cctype>
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
 * Convertit une string en minuscules (sans dependance a locale).
 */
[[nodiscard]] std::string to_lower(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

/**
 * Verifie qu'une extension de fichier est un YAML (.yaml ou .yml,
 * case-insensitive).
 */
[[nodiscard]] bool is_yaml_extension(const std::string& ext)
{
    const auto lower = to_lower(ext);
    return lower == ".yaml" || lower == ".yml";
}

/**
 * Valide le parametre {file} extrait de l'URL.
 *
 * Refuse :
 *   - Vide
 *   - Contient '/' ou '\\'
 *   - Contient ".." (segment de path)
 *   - Commence par '.' (fichier cache)
 *   - Extension differente de .yaml/.yml
 */
[[nodiscard]] bool is_valid_filename(const std::string& file)
{
    if (file.empty()) return false;
    if (file[0] == '.') return false;
    if (file.find('/') != std::string::npos) return false;
    if (file.find('\\') != std::string::npos) return false;
    if (file.find("..") != std::string::npos) return false;

    const auto dot = file.rfind('.');
    if (dot == std::string::npos) return false;
    return is_yaml_extension(file.substr(dot));
}

/**
 * Effectue le canonical check anti path-traversal. Retourne true si
 * canonical_target est strictement contenu dans canonical_configs.
 */
[[nodiscard]] bool is_within_configs(const fs::path& canonical_target,
                                     const fs::path& canonical_configs)
{
    const std::string target_str  = canonical_target.string();
    const std::string configs_str = canonical_configs.string();
    return target_str.size() > configs_str.size() + 1 &&
           target_str.compare(0, configs_str.size(), configs_str) == 0 &&
           (target_str[configs_str.size()] == '/' ||
            target_str[configs_str.size()] == '\\');
}

} // namespace anonyme


DeleteProjectHandler::DeleteProjectHandler(std::string configs_dir,
                                           std::string admin_role)
    : configs_dir_(std::move(configs_dir))
    , admin_role_(std::move(admin_role))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
DeleteProjectHandler::handle(const seastar::sstring&,
                             std::unique_ptr<seastar::http::request> req,
                             std::unique_ptr<seastar::http::reply> rep)
{
    // ─── 1. Garde role admin ─────────────────────────────────────
    if (auto guard = check_admin_role(*req, admin_role_); guard) {
        co_return std::move(guard);
    }

    // ─── 2. Extraction et validation du parametre {file} ─────────
    const seastar::sstring& file_param = req->get_path_param("file");
    const std::string file(file_param.data(), file_param.size());

    if (!is_valid_filename(file)) {
        if (auto log = spdlog::get("sea.http")) {
            log->warn(
                "DeleteProjectHandler: invalid filename '{}'", file);
        }
        co_return errors::make_error_reply(
            Status::bad_request, "VALIDATION_ERROR",
            "Invalid filename. Expected: <name>.yaml or <name>.yml "
            "without path separators.");
    }

    // ─── 3. Verification de l'existence du fichier ───────────────
    std::error_code ec;
    const fs::path configs_path(configs_dir_);
    const fs::path target = configs_path / file;

    if (!fs::exists(target, ec) || !fs::is_regular_file(target, ec)) {
        co_return errors::make_error_reply(
            Status::not_found, "NOT_FOUND",
            "Project file not found.");
    }

    // ─── 4. Canonical check anti path-traversal ──────────────────
    const fs::path canonical_target = fs::canonical(target, ec);
    if (ec) {
        if (auto log = spdlog::get("sea.http")) {
            log->error(
                "DeleteProjectHandler: canonical() failed for '{}': {}",
                target.string(), ec.message());
        }
        co_return errors::make_internal_error_reply();
    }
    const fs::path canonical_configs = fs::canonical(configs_path, ec);
    if (ec) {
        if (auto log = spdlog::get("sea.http")) {
            log->error(
                "DeleteProjectHandler: canonical() failed for configs_dir "
                "'{}': {}",
                configs_dir_, ec.message());
        }
        co_return errors::make_internal_error_reply();
    }

    if (!is_within_configs(canonical_target, canonical_configs)) {
        if (auto log = spdlog::get("sea.security")) {
            log->warn(
                "DeleteProjectHandler: path traversal attempt detected. "
                "Requested file '{}' resolves to '{}' outside configs_dir "
                "'{}'.",
                file,
                canonical_target.string(),
                canonical_configs.string());
        }
        co_return errors::make_error_reply(
            Status::bad_request, "VALIDATION_ERROR",
            "Invalid filename.");
    }

    // ─── 5. Suppression ──────────────────────────────────────────
    // fs::remove retourne false si le fichier n'existait pas (deja
    // gere par l'etape 3). Si erreur de permission ou autre, ec est
    // rempli et on retourne 500.
    if (!fs::remove(canonical_target, ec) || ec) {
        if (auto log = spdlog::get("sea.http")) {
            log->error(
                "DeleteProjectHandler: failed to delete '{}': {}",
                canonical_target.string(),
                ec ? ec.message() : "unknown error");
        }
        co_return errors::make_internal_error_reply();
    }

    // ─── 6. Reponse succes ───────────────────────────────────────
    json response;
    response["success"] = true;
    response["file"]    = file;

    rep->set_status(Status::ok);
    rep->write_body("application/json", response.dump());

    if (auto log = spdlog::get("sea.application")) {
        log->info(
            "DeleteProjectHandler: deleted '{}'", file);
    }

    co_return std::move(rep);
}

} // namespace sea::http::handlers::admin