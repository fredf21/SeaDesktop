#include "save_project_handler.h"

#include "../../errors/error_response_factory.h"
#include "http/utils/http_utils.h"
#include "import_yaml_schema_usecase.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <cctype>
#include <filesystem>
#include <fstream>
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


SaveProjectHandler::SaveProjectHandler(std::string configs_dir,
                                       std::string admin_role)
    : configs_dir_(std::move(configs_dir))
    , admin_role_(std::move(admin_role))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
SaveProjectHandler::handle(const seastar::sstring&,
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
                "SaveProjectHandler: invalid filename '{}'", file);
        }
        co_return errors::make_error_reply(
            Status::bad_request, "VALIDATION_ERROR",
            "Invalid filename. Expected: <name>.yaml or <name>.yml "
            "without path separators.");
    }

    // ─── 3. Verification de l'existence du fichier ───────────────
    // PUT remplace seulement, pas de creation. Utiliser POST pour
    // creer un nouveau projet.
    std::error_code ec;
    const fs::path configs_path(configs_dir_);
    const fs::path target = configs_path / file;

    if (!fs::exists(target, ec) || !fs::is_regular_file(target, ec)) {
        co_return errors::make_error_reply(
            Status::not_found, "NOT_FOUND",
            "Project file not found. Use POST /admin/projects to create.");
    }

    // ─── 4. Canonical check anti path-traversal ──────────────────
    const fs::path canonical_target  = fs::canonical(target, ec);
    if (ec) {
        if (auto log = spdlog::get("sea.http")) {
            log->error(
                "SaveProjectHandler: canonical() failed for '{}': {}",
                target.string(), ec.message());
        }
        co_return errors::make_internal_error_reply();
    }
    const fs::path canonical_configs = fs::canonical(configs_path, ec);
    if (ec) {
        if (auto log = spdlog::get("sea.http")) {
            log->error(
                "SaveProjectHandler: canonical() failed for configs_dir "
                "'{}': {}",
                configs_dir_, ec.message());
        }
        co_return errors::make_internal_error_reply();
    }

    if (!is_within_configs(canonical_target, canonical_configs)) {
        if (auto log = spdlog::get("sea.security")) {
            log->warn(
                "SaveProjectHandler: path traversal attempt detected. "
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

    // ─── 5. Lecture du body (YAML envoye par le client) ──────────
    const std::string body = co_await sea::http::utils::read_request_body(*req);
    if (body.empty()) {
        co_return errors::make_error_reply(
            Status::bad_request, "VALIDATION_ERROR",
            "Empty body. Expected YAML content.");
    }

    // ─── 6. Ecriture dans le fichier temporaire ──────────────────
    // On utilise <final>.tmp dans le meme dossier pour que le rename
    // final soit atomique (meme filesystem garantis).
    const fs::path tmp_path = canonical_target.string() + ".tmp";

    try {
        std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            if (auto log = spdlog::get("sea.http")) {
                log->error(
                    "SaveProjectHandler: failed to open tmp file '{}'",
                    tmp_path.string());
            }
            co_return errors::make_internal_error_reply();
        }
        out.write(body.data(), static_cast<std::streamsize>(body.size()));
        if (!out.good()) {
            // L'ecriture a echoue partiellement. Cleanup et erreur 500.
            out.close();
            fs::remove(tmp_path, ec);
            if (auto log = spdlog::get("sea.http")) {
                log->error(
                    "SaveProjectHandler: write to tmp file failed for '{}'",
                    tmp_path.string());
            }
            co_return errors::make_internal_error_reply();
        }
    } catch (const std::exception& e) {
        fs::remove(tmp_path, ec);
        if (auto log = spdlog::get("sea.http")) {
            log->error(
                "SaveProjectHandler: exception writing tmp '{}': {}",
                tmp_path.string(), e.what());
        }
        co_return errors::make_internal_error_reply();
    }

    // ─── 7. Validation du YAML via ImportYamlSchemaUseCase ───────
    // En plus de la validation syntaxe/schema, on verifie que le
    // champ project.name du YAML correspond au basename du fichier
    // (sans extension). Ex: fichier "TestDemo.yaml" doit contenir
    // project.name: "TestDemo".
    try {
        sea::application::ImportYamlSchemaUseCase importer;
        const auto project = importer.execute(tmp_path.string());

        // Verification de coherence nom de fichier <-> project.name
        const fs::path file_path(file);
        const std::string expected_name = file_path.stem().string();
        if (project.name != expected_name) {
            fs::remove(tmp_path, ec);
            if (auto log = spdlog::get("sea.http")) {
                log->info(
                    "SaveProjectHandler: name mismatch in '{}': "
                    "project.name='{}' but filename expects '{}'",
                    file, project.name, expected_name);
            }
            co_return errors::make_error_reply(
                Status::bad_request, "VALIDATION_ERROR",
                "project.name in YAML ('" + project.name +
                    "') must match filename ('" + expected_name + "').");
        }
    } catch (const std::exception& e) {
        // YAML invalide. Cleanup et retour du message d'erreur au client.
        fs::remove(tmp_path, ec);
        if (auto log = spdlog::get("sea.http")) {
            log->info(
                "SaveProjectHandler: YAML validation failed for '{}': {}",
                file, e.what());
        }
        co_return errors::make_error_reply(
            Status::bad_request, "VALIDATION_ERROR",
            std::string("Invalid YAML: ") + e.what());
    }

    // ─── 8. Rename atomique tmp -> final ─────────────────────────
    fs::rename(tmp_path, canonical_target, ec);
    if (ec) {
        // Cleanup du tmp si possible (le rename a echoue donc tmp existe encore).
        std::error_code ignore;
        fs::remove(tmp_path, ignore);
        if (auto log = spdlog::get("sea.http")) {
            log->error(
                "SaveProjectHandler: rename '{}' -> '{}' failed: {}",
                tmp_path.string(),
                canonical_target.string(),
                ec.message());
        }
        co_return errors::make_internal_error_reply();
    }

    // ─── 9. Reponse succes ───────────────────────────────────────
    json response;
    response["success"] = true;
    response["file"]    = file;

    rep->set_status(Status::ok);
    rep->write_body("application/json", response.dump());

    if (auto log = spdlog::get("sea.application")) {
        log->info(
            "SaveProjectHandler: saved '{}' ({} bytes)",
            file, body.size());
    }

    co_return std::move(rep);
}

} // namespace sea::http::handlers::admin