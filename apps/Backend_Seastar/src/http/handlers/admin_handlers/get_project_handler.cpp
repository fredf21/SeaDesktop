#include "get_project_handler.h"

#include "../../errors/error_response_factory.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace sea::http::handlers::admin {

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
 *
 * Cette validation est la premiere ligne de defense ; le canonical
 * check dans le handler est la deuxieme.
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

} // namespace anonyme


GetProjectHandler::GetProjectHandler(std::string configs_dir,
                                     std::string admin_role)
    : configs_dir_(std::move(configs_dir))
    , admin_role_(std::move(admin_role))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
GetProjectHandler::handle(const seastar::sstring&,
                          std::unique_ptr<seastar::http::request> req,
                          std::unique_ptr<seastar::http::reply> rep)
{
    // ─── 1. Garde role admin ─────────────────────────────────────
    if (auto guard = check_admin_role(*req, admin_role_); guard) {
        co_return std::move(guard);
    }

    // ─── 2. Extraction et validation du parametre {file} ─────────
    // Le parametre est extrait par le match_rule construit par
    // build_match_rule_from_template avec le template
    // "/admin/projects/{file}". Seastar le rend disponible via
    // req->get_path_param("file").
    const seastar::sstring& file_param = req->get_path_param("file");
    const std::string file(file_param.data(), file_param.size());

    if (!is_valid_filename(file)) {
        if (auto log = spdlog::get("sea.http")) {
            log->warn(
                "GetProjectHandler: invalid filename '{}'", file);
        }
        co_return errors::make_error_reply(
            Status::bad_request, "VALIDATION_ERROR",
            "Invalid filename. Expected: <name>.yaml or <name>.yml "
            "without path separators.");
    }

    // ─── 3. Construction du path et protection path traversal ────
    std::error_code ec;
    const fs::path configs_path(configs_dir_);
    const fs::path target = configs_path / file;

    // Verification que le fichier existe avant canonical (qui throw
    // si le fichier n'existe pas).
    if (!fs::exists(target, ec) || !fs::is_regular_file(target, ec)) {
        co_return errors::make_error_reply(
            Status::not_found, "NOT_FOUND",
            "Project file not found.");
    }

    // Canonical check : resout les symlinks et compare au dossier
    // configs canonicalise. Refuse si le fichier reel est en dehors.
    const fs::path canonical_target = fs::canonical(target, ec);
    if (ec) {
        if (auto log = spdlog::get("sea.http")) {
            log->error(
                "GetProjectHandler: canonical() failed for '{}': {}",
                target.string(), ec.message());
        }
        co_return errors::make_internal_error_reply();
    }

    const fs::path canonical_configs = fs::canonical(configs_path, ec);
    if (ec) {
        if (auto log = spdlog::get("sea.http")) {
            log->error(
                "GetProjectHandler: canonical() failed for configs_dir "
                "'{}': {}",
                configs_dir_, ec.message());
        }
        co_return errors::make_internal_error_reply();
    }

    // Verifie que canonical_target commence bien par canonical_configs
    // suivi d'un separateur. Sans ca, "configs_dir_evil/X.yaml" passerait
    // alors que configs_dir = "configs_dir".
    const std::string canonical_target_str  = canonical_target.string();
    const std::string canonical_configs_str = canonical_configs.string();
    const bool in_configs =
        canonical_target_str.size() > canonical_configs_str.size() + 1 &&
        canonical_target_str.compare(0, canonical_configs_str.size(),
                                     canonical_configs_str) == 0 &&
        (canonical_target_str[canonical_configs_str.size()] == '/' ||
         canonical_target_str[canonical_configs_str.size()] == '\\');

    if (!in_configs) {
        if (auto log = spdlog::get("sea.security")) {
            log->warn(
                "GetProjectHandler: path traversal attempt detected. "
                "Requested file '{}' resolves to '{}' outside configs_dir "
                "'{}'.",
                file, canonical_target_str, canonical_configs_str);
        }
        co_return errors::make_error_reply(
            Status::bad_request, "VALIDATION_ERROR",
            "Invalid filename.");
    }

    // ─── 4. Lecture du fichier ───────────────────────────────────
    std::string content;
    try {
        std::ifstream f(canonical_target);
        if (!f.is_open()) {
            if (auto log = spdlog::get("sea.http")) {
                log->error(
                    "GetProjectHandler: failed to open '{}'",
                    canonical_target_str);
            }
            co_return errors::make_internal_error_reply();
        }

        std::ostringstream ss;
        ss << f.rdbuf();
        content = ss.str();
    } catch (const std::exception& e) {
        if (auto log = spdlog::get("sea.http")) {
            log->error(
                "GetProjectHandler: error reading '{}': {}",
                canonical_target_str, e.what());
        }
        co_return errors::make_internal_error_reply();
    }

    // ─── 5. Reponse ──────────────────────────────────────────────
    rep->set_status(Status::ok);
    rep->write_body("application/x-yaml", content);

    if (auto log = spdlog::get("sea.application")) {
        log->debug(
            "GetProjectHandler: served '{}' ({} bytes)",
            file, content.size());
    }

    co_return std::move(rep);
}

} // namespace sea::http::handlers::admin