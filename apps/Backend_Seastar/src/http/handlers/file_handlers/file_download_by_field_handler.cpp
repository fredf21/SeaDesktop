#include "file_download_by_field_handler.h"
#include "../access_control/resource_authorization_helper.h"
#include "../../utils/http_utils.h"

#include "access_control/crud_operation.h"
#include "fileservice.h"
#include "runtime/generic_crud_engine.h"
#include "runtime/schema_runtime_registry.h"

#include "entity.h"
#include "field.h"
#include "exception_handling.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <utility>
#include <variant>

namespace sea::http::handlers::files {

using json = nlohmann::json;

namespace {

// Construit un Content-Disposition correct pour un download.
//
// On utilise "inline" plutôt qu'"attachment" pour permettre au navigateur
// d'afficher l'image/PDF directement si c'est ce qu'attend l'utilisateur.
// Si le client veut forcer un download-to-disk, il peut utiliser
// l'attribut `download` côté HTML, ou ajouter `?download=1` côté API
// (à ajouter plus tard si besoin).
//
// Le filename est encodé selon RFC 6266 :
//   - filename="..." pour les clients ASCII basique
//   - filename*=UTF-8''<percent-encoded> pour l'unicode
//
// Pour le MVP, on échappe les guillemets et on tronque les caractères
// non-ASCII en '?' dans le filename simple. La version étendue serait
// du percent-encoding RFC 5987 - à ajouter plus tard.
std::string build_content_disposition(const std::string& original_name) {
    std::string safe;
    safe.reserve(original_name.size());
    for (char c : original_name) {
        if (static_cast<unsigned char>(c) < 0x20 || c == '"' || c == '\\') {
            safe.push_back('_');
        } else if (static_cast<unsigned char>(c) > 0x7E) {
            // Non-ASCII : remplace par '?' (placeholder)
            safe.push_back('?');
        } else {
            safe.push_back(c);
        }
    }
    return "inline; filename=\"" + safe + "\"";
}

} // namespace

FileDownloadByFieldHandler::FileDownloadByFieldHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry> registry,
    std::shared_ptr<sea::application::FileService> file_service,
    std::string entity_name,
    std::string field_name,
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper)
    : crud_engine_(std::move(crud_engine))
    , registry_(std::move(registry))
    , file_service_(std::move(file_service))
    , entity_name_(std::move(entity_name))
    , field_name_(std::move(field_name))
    , auth_helper_(std::move(auth_helper))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
FileDownloadByFieldHandler::handle(const seastar::sstring&,
                                   std::unique_ptr<seastar::http::request> req,
                                   std::unique_ptr<seastar::http::reply> rep)
{
    auto log = spdlog::get("sea.http");

    // ─── Validation des params URL ────────────────────────
    const auto id = req->get_path_param("id");
    if (id.empty()) {
        rep->set_status(seastar::http::reply::status_type::bad_request);
        rep->write_body("application/json",
                        json{{"error", "Parametre 'id' manquant."}}.dump());
        co_return std::move(rep);
    }

    // ─── Récupération de l'entité depuis le registry ──────
    const auto* entity = registry_->find_entity(entity_name_);
    if (entity == nullptr) {
        rep->set_status(seastar::http::reply::status_type::not_found);
        rep->write_body("application/json",
                        json{{"error", "Entite inconnue."}}.dump());
        co_return std::move(rep);
    }

    // ─── Validation que field_name_ est bien un champ File ─
    // Cette vérification est statique au boot (le route_registration
    // ne crée la route que si le champ existe). On la refait ici par
    // sécurité — un changement dynamique de schema (cf. hot-reload
    // futur) pourrait casser cette invariant.
    const sea::domain::Field* field = nullptr;
    for (const auto& f : entity->fields) {
        if (f.name == field_name_ && f.is_file_field()) {
            field = &f;
            break;
        }
    }
    if (field == nullptr) {
        rep->set_status(seastar::http::reply::status_type::not_found);
        rep->write_body("application/json",
                        json{{"error", "Champ inconnu ou non-file."}}.dump());
        co_return std::move(rep);
    }

    try {
        // ─── Chargement du record de l'entité parente ─────
        const auto record = co_await crud_engine_->get_by_id(
            entity_name_, std::string(id));

        if (!record.has_value()) {
            rep->set_status(seastar::http::reply::status_type::not_found);
            rep->write_body("application/json",
                            json{{"error", "Enregistrement introuvable."}}.dump());
            co_return std::move(rep);
        }

        // ─── ABAC : check Read sur l'entité parente ───────
        // L'autorisation est entièrement déléguée à la policy de
        // l'entité. Si le subject ne peut pas lire le User, il ne
        // peut pas voir son avatar.
        if (auth_helper_) {
            const std::string current_json = sea::http::utils::record_to_json(*record);
            const auto subject = auth_helper_->build_subject_from_headers(*req);
            const std::string path_str(req->_url.data(), req->_url.size());
            const auto context = auth_helper_->build_context(*req, path_str);

            const auto check = auth_helper_->check_single(
                entity_name_,
                sea::domain::access_control::CrudOperation::GetById,
                subject,
                current_json,
                context
                );

            if (!check.allowed) {
                rep->set_status(seastar::http::reply::status_type::forbidden);
                rep->write_body("application/json",
                                json{{"error", "Forbidden"},
                                     {"message", check.reason}}.dump());
                co_return std::move(rep);
            }
        }

        // ─── Extraction de l'UUID du champ File ───────────
        const auto field_it = record->find(field_name_);
        if (field_it == record->end()) {
            rep->set_status(seastar::http::reply::status_type::not_found);
            rep->write_body("application/json",
                            json{{"error", "Champ vide sur ce record."}}.dump());
            co_return std::move(rep);
        }

        if (!std::holds_alternative<std::string>(field_it->second)) {
            // Le champ existe mais n'est pas une string — corruption ?
            log->error("FileDownloadByFieldHandler: field '{}' on {}/{} "
                       "is not a string (corrupted record?)",
                       field_name_, entity_name_, id);
            rep->set_status(seastar::http::reply::status_type::internal_server_error);
            rep->write_body("application/json",
                            json{{"error", "Type de valeur invalide pour ce champ."}}.dump());
            co_return std::move(rep);
        }

        const std::string uuid = std::get<std::string>(field_it->second);
        if (uuid.empty()) {
            rep->set_status(seastar::http::reply::status_type::not_found);
            rep->write_body("application/json",
                            json{{"error", "Aucun fichier attache."}}.dump());
            co_return std::move(rep);
        }

        // ─── Download via FileService ─────────────────────
        // FileService::download retourne {metadata, content} ou nullopt
        // si l'UUID est inconnu dans sea_files. L'inconnu en sea_files
        // alors qu'on a une FK qui pointe = corruption (la FK SQL devrait
        // l'empêcher) → 404 pour rester poli côté client.
        auto download_opt = co_await file_service_->download(uuid);
        if (!download_opt.has_value()) {
            log->warn(
                "FileDownloadByFieldHandler: uuid={} referenced by {}/{}/{} "
                "but not found in sea_files",
                uuid, entity_name_, id, field_name_);
            rep->set_status(seastar::http::reply::status_type::not_found);
            rep->write_body("application/json",
                            json{{"error", "Fichier introuvable."}}.dump());
            co_return std::move(rep);
        }

        // ─── Réponse binaire ──────────────────────────────
        rep->set_status(seastar::http::reply::status_type::ok);

        // Content-Type : type MIME stocké dans sea_files au moment de
        // l'upload. Si vide (cas dégénéré), fallback générique.
        const std::string content_type =
            download_opt->metadata.mime_type.empty()
                ? std::string("application/octet-stream")
                : download_opt->metadata.mime_type;

        // Content-Disposition : "inline" + filename original.
        rep->add_header(
            "Content-Disposition",
            build_content_disposition(download_opt->metadata.original_name));

        // write_body est binary-safe (std::string est binaire en C++17+).
        rep->write_body(content_type, std::move(download_opt->content));

        log->info("FileDownloadByFieldHandler: served uuid={} ({} bytes, {})",
                  uuid,
                  download_opt->metadata.size_bytes,
                  download_opt->metadata.mime_type);

        co_return std::move(rep);

    } catch (const sea_errors_handling::StorageException& e) {
        // Storage I/O échoué (fichier disparu du disque ? perms ?)
        log->error("FileDownloadByFieldHandler: StorageException: {}", e.what());
        rep->set_status(seastar::http::reply::status_type::not_found);
        rep->write_body("application/json",
                        json{{"error", "Fichier physique introuvable."}}.dump());
        co_return std::move(rep);

    } catch (const std::exception& e) {
        log->error("FileDownloadByFieldHandler: exception: {}", e.what());
        rep->set_status(seastar::http::reply::status_type::internal_server_error);
        rep->write_body("application/json",
                        json{{"error", e.what()}}.dump());
        co_return std::move(rep);
    }
}

} // namespace sea::http::handlers::files