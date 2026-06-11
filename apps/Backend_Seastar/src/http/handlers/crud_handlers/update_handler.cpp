#include "update_handler.h"
#include "../access_control/resource_authorization_helper.h"
#include "../../utils/http_utils.h"
#include "../../errors/error_response_factory.h"

#include "authservice.h"
#include "access_control/crud_operation.h"
#include "runtime/generic_crud_engine.h"
#include "runtime/json_record_parser.h"
#include "runtime/schema_runtime_registry.h"
#include "persistence/i_generic_repository.h"

#include <nlohmann/json.hpp>
#include <seastar/core/thread.hh>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <utility>
#include <variant>

namespace sea::http::handlers::crud {

using json = nlohmann::json;
namespace errors = sea::http::errors;
using Status = seastar::http::reply::status_type;

UpdateHandler::UpdateHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry> registry,
    std::shared_ptr<sea::application::AuthService> auth_service,
    std::string entity_name,
    std::shared_ptr<IBlockingExecutor> blocking_executor,
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper,
    std::shared_ptr<sea::http::handlers::file_upload::FileUploadExtractor> file_extractor)
    : crud_engine_(std::move(crud_engine))
    , registry_(std::move(registry))
    , auth_service_(std::move(auth_service))
    , entity_name_(std::move(entity_name))
    , blocking_executor_(std::move(blocking_executor))
    , auth_helper_(std::move(auth_helper))
    , file_extractor_(std::move(file_extractor))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
UpdateHandler::handle(const seastar::sstring&,
                      std::unique_ptr<seastar::http::request> req,
                      std::unique_ptr<seastar::http::reply> rep)
{
    const auto id = std::string(sea::http::utils::strip_leading_slash(req->get_path_param("id")));
    if (id.empty()) {
        co_return errors::make_error_reply(
            Status::bad_request, "BAD_REQUEST", "Parametre 'id' manquant.");
    }

    const auto* entity = registry_->find_entity(entity_name_);
    if (entity == nullptr) {
        co_return errors::make_error_reply(
            Status::not_found, "NOT_FOUND", "Entite inconnue.");
    }

    const bool is_multipart =
        (file_extractor_ != nullptr) &&
        sea::http::handlers::file_upload::FileUploadExtractor::is_multipart_request(*req);

    try {
        // ─── ABAC check + récupération de l'état avant modification ──
        // On charge la ressource existante DANS TOUS LES CAS lorsque
        // l'entité a des champs File, parce qu'on a besoin des anciens
        // UUIDs pour les release() après l'update.
        const bool entity_has_files = std::any_of(
            entity->fields.begin(), entity->fields.end(),
            [](const auto& f) { return f.is_file_field(); });

        std::optional<sea::infrastructure::runtime::DynamicRecord> existing_record;

        if (auth_helper_ || entity_has_files) {
            existing_record = co_await crud_engine_->get_by_id(
                entity_name_, std::string(id));

            if (!existing_record.has_value()) {
                co_return errors::make_error_reply(
                    Status::not_found, "NOT_FOUND",
                    "Enregistrement introuvable.");
            }

            if (auth_helper_) {
                const std::string current_json =
                    sea::http::utils::record_to_json(*existing_record);
                const auto subject = auth_helper_->build_subject_from_headers(*req);
                const std::string path_str(req->_url.data(), req->_url.size());
                const auto context = auth_helper_->build_context(*req, path_str);

                const auto check = auth_helper_->check_single(
                    entity_name_,
                    sea::domain::access_control::CrudOperation::Update,
                    subject,
                    current_json,
                    context
                    );

                if (!check.allowed) {
                    co_return errors::make_error_reply(
                        Status::forbidden, "AUTHORIZATION_ERROR",
                        check.reason.empty()
                            ? "Acces refuse."
                            : check.reason);
                }
            }
        }

        const std::string body = co_await sea::http::utils::read_request_body(*req);

        // ─── Parsing du body selon le mode ──────────────────
        sea::infrastructure::runtime::DynamicRecord record;
        sea::http::utils::multipart::ParsedMultipart parsed_multipart;
        sea::infrastructure::runtime::JsonRecordParser parser;

        if (is_multipart) {
            const auto content_type_h = req->get_header("Content-Type");
            auto boundary = sea::http::utils::multipart::extract_boundary(
                std::string_view(content_type_h.data(), content_type_h.size()));

            if (!boundary.has_value()) {
                co_return errors::make_error_reply(
                    Status::bad_request, "BAD_REQUEST",
                    "Content-Type multipart sans boundary.");
            }

            try {
                parsed_multipart = sea::http::utils::multipart::parse(body, *boundary);
            } catch (const std::exception& e) {
                co_return errors::make_error_reply(
                    Status::bad_request, "BAD_REQUEST",
                    std::string("Multipart invalide: ") + e.what());
            }

            nlohmann::json json_from_text_parts = nlohmann::json::object();
            for (const auto& tp : parsed_multipart.text_parts) {
                json_from_text_parts[tp.name] = tp.value;
            }

            try {
                record = parser.parse(*entity, json_from_text_parts.dump());
            } catch (const std::exception& e) {
                co_return errors::make_error_reply(
                    Status::bad_request, "VALIDATION_ERROR",
                    std::string("Champs texte invalides: ") + e.what());
            }
        } else {
            record = parser.parse(*entity, body);
        }

        // ─── Hash password si présent ───────────────────────
        const auto password_it = record.find("password");
        if (password_it != record.end()) {
            const auto plain_password = sea::http::utils::dynamic_value_to_string(password_it->second);
            if (!plain_password.has_value()) {
                co_return errors::make_error_reply(
                    Status::bad_request, "VALIDATION_ERROR",
                    "Password invalide.");
            }

            const auto hashed_password = co_await blocking_executor_->submit(
                [auth_service = auth_service_,
                 plain = *plain_password] {
                    return auth_service->hash_password(plain);
                });
            record["password"] = hashed_password;
        }

        // ─── Collecte des anciens UUIDs File ────────────────
        // Pour chaque champ File qui sera modifié par l'update,
        // on note l'ancien UUID afin de pouvoir le release() après
        // commit (selon son on_delete config).
        //
        // Si l'update ne touche pas le champ File, on laisse tomber
        // (pas de release).
        struct OldFileRef {
            std::string uuid;
            sea::domain::OnDeleteFile on_delete_rule;
        };
        std::vector<OldFileRef> old_file_refs;

        if (entity_has_files && existing_record.has_value()) {
            for (const auto& field : entity->fields) {
                if (!field.is_file_field()) continue;

                // L'update touche-t-il ce champ ?
                if (record.find(field.name) == record.end()) {
                    continue;
                }

                const auto old_it = existing_record->find(field.name);
                if (old_it == existing_record->end()) {
                    continue;
                }
                if (!std::holds_alternative<std::string>(old_it->second)) {
                    continue;
                }
                const std::string old_uuid = std::get<std::string>(old_it->second);
                if (old_uuid.empty()) {
                    continue;
                }
                old_file_refs.push_back({old_uuid, field.file_config->on_delete});
            }
        }

        // ─── Transaction : upload + update + retain ─────────
        sea::http::handlers::file_upload::ExtractionResult extraction{};
        sea::infrastructure::runtime::GenericCrudEngine::OperationResult op_result{};
        auto tx = co_await crud_engine_->get_repository()->in_transaction(
            [this, entity, id_str = std::string(id), &record, &extraction,
             &op_result, is_multipart, &parsed_multipart]() -> seastar::future<bool>
            {
                // 1. Upload des nouveaux fichiers
                if (file_extractor_ != nullptr) {
                    try {
                        if (is_multipart) {
                            for (const auto& fp : parsed_multipart.file_parts) {
                                const sea::domain::Field* field = nullptr;
                                for (const auto& f : entity->fields) {
                                    if (f.name == fp.name && f.is_file_field()) {
                                        field = &f;
                                        break;
                                    }
                                }
                                if (field == nullptr) continue;

                                auto upload = co_await file_extractor_->upload_single_part(
                                    *field, fp);
                                record[field->name] = upload.uuid;
                                extraction.uploaded_uuids.push_back(upload.uuid);
                                extraction.had_files = true;
                            }
                        } else {
                            extraction = co_await file_extractor_->extract_from_json_record(
                                *entity, record);
                        }
                    } catch (const std::exception& e) {
                        spdlog::get("sea.http")->error(
                            "UpdateHandler: file extraction failed: {}", e.what());
                        co_return false;
                    }
                }

                // 2. UPDATE entité
                op_result = co_await crud_engine_->update(entity_name_, id_str, record);
                if (!op_result.success) {
                    co_return false;
                }

                // 3. retain nouveaux UUIDs
                if (extraction.had_files) {
                    const bool retain_ok = co_await file_extractor_->commit(extraction);
                    if (!retain_ok) {
                        co_return false;
                    }
                }

                co_return true;
            });

        if (!tx.committed) {
            if (extraction.had_files) {
                co_await file_extractor_->rollback(extraction);
            }

            if (!op_result.success && !op_result.errors.empty()) {
                // Concatene les erreurs metier en un message unique.
                std::string combined;
                for (std::size_t i = 0; i < op_result.errors.size(); ++i) {
                    if (i != 0) combined += "; ";
                    combined += op_result.errors[i];
                }
                co_return errors::make_error_reply(
                    Status::bad_request, "VALIDATION_ERROR", combined);
            }

            // Echec sans erreur metier explicite : interne.
            // Logue pour investigation - cas suspect, ne devrait pas arriver.
            spdlog::get("sea.http")->error(
                "UpdateHandler: transaction echouee sans message d'erreur "
                "(entity={}, id={})", entity_name_, id);
            co_return errors::make_internal_error_reply();
        }

        // ─── Release des anciens UUIDs (hors transaction) ────
        // Maintenant que l'update a réussi, on déréférence les anciens
        // fichiers selon leur on_delete config. Best-effort : si un
        // release échoue, on log mais on ne fail pas la réponse — le
        // client a obtenu l'update qu'il demandait.
        for (const auto& old : old_file_refs) {
            try {
                co_await file_extractor_->release_old_uuid(old.uuid, old.on_delete_rule);
            } catch (const std::exception& e) {
                spdlog::get("sea.http")->error(
                    "UpdateHandler: release of old uuid={} failed: {}",
                    old.uuid, e.what());
            }
        }

        rep->set_status(Status::ok);
        rep->write_body("application/json",
                        sea::http::utils::record_to_json(*op_result.record));
        co_return std::move(rep);

    } catch (const errors::HttpException& e) {
        // Erreur metier deja typee : on respecte son statut.
        co_return errors::make_error_reply(e);
    } catch (const std::exception& e) {
        // Erreur generique : on logue et on retourne 400.
        spdlog::get("sea.http")->warn(
            "UpdateHandler: unhandled exception: {}", e.what());
        co_return errors::make_error_reply(
            Status::bad_request, "BAD_REQUEST",
            std::string("Erreur: ") + e.what());
    }
}

} // namespace sea::http::handlers::crud