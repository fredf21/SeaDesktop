#include "delete_handler.h"
#include "../access_control/resource_authorization_helper.h"
#include "../../utils/http_utils.h"

#include "access_control/crud_operation.h"
#include "http/handlers/file_handlers/file_upload_extractor.h"
#include "runtime/generic_crud_engine.h"
#include "runtime/schema_runtime_registry.h"
#include "persistence/i_generic_repository.h"

#include "entity.h"
#include "field.h"
#include "file_field_config.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <utility>
#include <variant>
#include <vector>

namespace sea::http::handlers::crud {

using json = nlohmann::json;

DeleteHandler::DeleteHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::string entity_name,
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper,
    std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry> registry,
    std::shared_ptr<sea::http::handlers::file_upload::FileUploadExtractor> file_extractor)
    : crud_engine_(std::move(crud_engine))
    , entity_name_(std::move(entity_name))
    , auth_helper_(std::move(auth_helper))
    , registry_(std::move(registry))
    , file_extractor_(std::move(file_extractor))
{
}

// ─────────────────────────────────────────────────────────────
// Helpers locaux
// ─────────────────────────────────────────────────────────────
namespace {

// Référence à un fichier attaché à l'entité supprimée.
// Collectée AVANT le DELETE pour pouvoir release() après commit.
struct AttachedFile {
    std::string field_name;
    std::string uuid;
    sea::domain::OnDeleteFile rule;
};

// Pour une entité donnée et son record existant, retourne la liste
// des fichiers actuellement attachés (UUID non vide).
std::vector<AttachedFile>
collect_attached_files(
    const sea::domain::Entity& entity,
    const sea::infrastructure::runtime::DynamicRecord& record)
{
    std::vector<AttachedFile> result;
    for (const auto& field : entity.fields) {
        if (!field.is_file_field()) continue;

        const auto it = record.find(field.name);
        if (it == record.end()) continue;
        if (!std::holds_alternative<std::string>(it->second)) continue;

        const std::string uuid = std::get<std::string>(it->second);
        if (uuid.empty()) continue;

        result.push_back({
            field.name,
            uuid,
            field.file_config->on_delete
        });
    }
    return result;
}

} // namespace

seastar::future<std::unique_ptr<seastar::http::reply>>
DeleteHandler::handle(const seastar::sstring&,
                      std::unique_ptr<seastar::http::request> req,
                      std::unique_ptr<seastar::http::reply> rep)
{
    const auto id = req->get_path_param("id");
    if (id.empty()) {
        rep->set_status(seastar::http::reply::status_type::bad_request);
        rep->write_body("application/json",
                        json{{"error", "Parametre 'id' manquant."}}.dump());
        co_return std::move(rep);
    }

    try {
        // ─── Récupération du schéma (si registry injecté) ────
        // Si registry n'est PAS injecté, on bypass toute la logique
        // file et le handler retrouve son comportement d'origine.
        const sea::domain::Entity* entity = nullptr;
        bool entity_has_files = false;

        if (registry_ != nullptr) {
            entity = registry_->find_entity(entity_name_);
            if (entity != nullptr) {
                entity_has_files = std::any_of(
                    entity->fields.begin(), entity->fields.end(),
                    [](const auto& f) { return f.is_file_field(); });
            }
        }

        // ─── Chargement de l'enregistrement existant ─────────
        // Nécessaire pour :
        //   - l'ABAC (existait déjà)
        //   - la gestion des champs File (nouveau)
        std::optional<sea::infrastructure::runtime::DynamicRecord> existing_record;
        if (auth_helper_ || entity_has_files) {
            existing_record = co_await crud_engine_->get_by_id(
                entity_name_, std::string(id));

            if (!existing_record.has_value()) {
                rep->set_status(seastar::http::reply::status_type::not_found);
                rep->write_body("application/json",
                                json{{"error", "Enregistrement introuvable."}}.dump());
                co_return std::move(rep);
            }
        }

        // ─── Check ABAC ──────────────────────────────────────
        if (auth_helper_) {
            const std::string current_json =
                sea::http::utils::record_to_json(*existing_record);
            const auto subject = auth_helper_->build_subject_from_headers(*req);
            const std::string path_str(req->_url.data(), req->_url.size());
            const auto context = auth_helper_->build_context(*req, path_str);

            const auto check = auth_helper_->check_single(
                entity_name_,
                sea::domain::access_control::CrudOperation::Delete,
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

        // ─── Gestion des champs File (si applicable) ─────────
        std::vector<AttachedFile> attached_files;

        if (entity_has_files && existing_record.has_value()) {
            attached_files = collect_attached_files(*entity, *existing_record);

            // Règle on_delete=Restrict : on REFUSE le DELETE si au moins
            // un champ File en mode Restrict pointe vers un fichier.
            // Cela empêche la suppression "involontaire" de l'entité
            // qui détacherait des fichiers critiques (contrats, etc.).
            //
            // Le client doit d'abord détacher/remplacer le fichier
            // (via PUT/PATCH) avant de pouvoir supprimer l'entité.
            for (const auto& af : attached_files) {
                if (af.rule == sea::domain::OnDeleteFile::Restrict) {
                    rep->set_status(seastar::http::reply::status_type::conflict);
                    rep->write_body(
                        "application/json",
                        json{
                            {"error", "Conflict"},
                            {"message",
                             "L'entite '" + entity_name_ +
                                 "' ne peut pas etre supprimee : le champ file '" +
                                 af.field_name + "' a une regle on_delete=restrict. "
                                                 "Detacher/remplacer le fichier avant suppression."}
                        }.dump());
                    co_return std::move(rep);
                }
            }
        }

        // ─── DELETE entité ──────────────────────────────────
        // Justification du choix release-après-commit :
        // si l'on faisait les release DANS la tx, un échec de release
        // ferait rollback du DELETE entité, alors que pour l'utilisateur
        // l'opération principale (la suppression) a réussi. On préfère
        // que l'entité soit bien supprimée et que les éventuels fichiers
        // orphelins soient récupérés par release_orphans offline si
        // jamais le release échoue.
        bool deleted = false;
        if (file_extractor_ != nullptr && entity_has_files) {
            // Avec gestion files : utilise une transaction explicite
            // pour wrap le DELETE (cohérence avec Create/Update).
            auto tx = co_await crud_engine_->get_repository()->in_transaction(
                [this, id_str = std::string(id), &deleted]() -> seastar::future<bool> {
                    deleted = co_await crud_engine_->remove(entity_name_, id_str);
                    co_return deleted;
                });

            if (!tx.committed) {
                rep->set_status(seastar::http::reply::status_type::internal_server_error);
                rep->write_body("application/json",
                                json{{"error", "Transaction DELETE echouee."}}.dump());
                co_return std::move(rep);
            }
        } else {
            // Mode legacy : pas de tx explicite, comportement d'origine.
            deleted = co_await crud_engine_->remove(entity_name_, std::string(id));
        }

        if (!deleted) {
            rep->set_status(seastar::http::reply::status_type::not_found);
            rep->write_body("application/json",
                            json{{"error", "Enregistrement introuvable."}}.dump());
            co_return std::move(rep);
        }

        // ─── Release des UUIDs (Cascade/SetNull, best-effort) ─
        // Le DELETE entité a réussi : on déréférence les fichiers
        // attachés selon leur on_delete config.
        //
        //   Cascade : décrémente reference_count, et si 0 → supprime
        //             la row sea_files + fichier physique.
        //   SetNull : décrémente reference_count uniquement. Fichier
        //             gardé pour collecte offline si orphelin.
        //
        // (Restrict a déjà été filtré plus haut.)
        if (file_extractor_ != nullptr) {
            for (const auto& af : attached_files) {
                try {
                    co_await file_extractor_->release_old_uuid(af.uuid, af.rule);
                } catch (const std::exception& e) {
                    spdlog::get("sea.http")->error(
                        "DeleteHandler: release of uuid={} (field={}) failed: {}",
                        af.uuid, af.field_name, e.what());
                }
            }
        }

        rep->set_status(seastar::http::reply::status_type::ok);
        rep->write_body("application/json", json{{"message", "Deleted"}}.dump());
        co_return std::move(rep);

    } catch (const std::exception& e) {
        rep->set_status(seastar::http::reply::status_type::internal_server_error);
        rep->write_body("application/json", json{{"error", e.what()}}.dump());
        co_return std::move(rep);
    }
}

} // namespace sea::http::handlers::crud