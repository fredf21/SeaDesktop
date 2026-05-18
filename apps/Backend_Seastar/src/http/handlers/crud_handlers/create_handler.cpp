#include "create_handler.h"
#include "../access_control/resource_authorization_helper.h"
#include "../../utils/http_utils.h"

#include "authservice.h"
#include "access_control/crud_operation.h"
#include "runtime/generic_crud_engine.h"
#include "runtime/json_record_parser.h"
#include "runtime/schema_runtime_registry.h"
#include "persistence/i_generic_repository.h"

#include <nlohmann/json.hpp>
#include <seastar/core/thread.hh>
#include <spdlog/spdlog.h>
#include <sstream>
#include <utility>

namespace sea::http::handlers::crud {

using json = nlohmann::json;

CreateHandler::CreateHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry> registry,
    std::string entity_name,
    std::shared_ptr<sea::application::AuthService> auth_service,
    sea::domain::DatabaseType db_type,
    std::shared_ptr<IBlockingExecutor> blocking_executor,
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper,
    std::shared_ptr<sea::http::handlers::file_upload::FileUploadExtractor> file_extractor)
    : crud_engine_(std::move(crud_engine))
    , registry_(std::move(registry))
    , entity_name_(std::move(entity_name))
    , auth_service_(std::move(auth_service))
    , db_type_(db_type)
    , blocking_executor_(std::move(blocking_executor))
    , auth_helper_(std::move(auth_helper))
    , file_extractor_(std::move(file_extractor))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
CreateHandler::handle(const seastar::sstring&,
                      std::unique_ptr<seastar::http::request> req,
                      std::unique_ptr<seastar::http::reply> rep)
{
    const auto* entity = registry_->find_entity(entity_name_);
    if (entity == nullptr) {
        rep->set_status(seastar::http::reply::status_type::not_found);
        rep->write_body("application/json", json{{"error", "Entite inconnue."}}.dump());
        co_return std::move(rep);
    }

    // ─────────────────────────────────────────────────────────
    // Détection du mode : multipart/form-data vs application/json
    //
    // Note : la détection multipart nécessite à la fois Seastar
    // (req.is_multi_part()) et notre extract_boundary. Si l'extractor
    // n'est pas injecté, on traite tout comme JSON même si le Content-Type
    // est multipart (le JsonRecordParser produira alors une erreur claire,
    // gérée par le catch global).
    // ─────────────────────────────────────────────────────────
    const bool is_multipart =
        (file_extractor_ != nullptr) &&
        sea::http::handlers::file_upload::FileUploadExtractor::is_multipart_request(*req);

    try {
        const std::string body = co_await sea::http::utils::read_request_body(*req);

        // ─── Construction initiale du record ─────────────────
        // Mode multipart : parser le body, extraire les text parts,
        // les sérialiser en JSON pour les passer au JsonRecordParser
        // (qui typera correctement les champs int/bool/etc.).
        // Les UPLOADS de fichiers se feront PLUS BAS, à l'intérieur
        // de la transaction.
        //
        // Mode JSON : on parse directement comme avant.
        sea::infrastructure::runtime::DynamicRecord record;
        sea::http::utils::multipart::ParsedMultipart parsed_multipart;
        sea::infrastructure::runtime::JsonRecordParser parser;

        if (is_multipart) {
            // Pre-parse du body multipart pour récupérer text parts +
            // file parts (qui seront uploadés dans la tx ci-dessous).
            const auto content_type_h = req->get_header("Content-Type");
            auto boundary = sea::http::utils::multipart::extract_boundary(
                std::string_view(content_type_h.data(), content_type_h.size()));

            if (!boundary.has_value()) {
                rep->set_status(seastar::http::reply::status_type::bad_request);
                rep->write_body("application/json",
                                json{{"error", "Content-Type multipart sans boundary."}}.dump());
                co_return std::move(rep);
            }

            try {
                parsed_multipart = sea::http::utils::multipart::parse(body, *boundary);
            } catch (const std::exception& e) {
                rep->set_status(seastar::http::reply::status_type::bad_request);
                rep->write_body("application/json",
                                json{{"error", std::string("Multipart invalide: ") + e.what()}}.dump());
                co_return std::move(rep);
            }

            // Construit un JSON équivalent depuis les text parts pour
            // que JsonRecordParser puisse typer correctement.
            nlohmann::json json_from_text_parts = nlohmann::json::object();
            for (const auto& tp : parsed_multipart.text_parts) {
                json_from_text_parts[tp.name] = tp.value;
            }

            try {
                record = parser.parse(*entity, json_from_text_parts.dump());
            } catch (const std::exception& e) {
                rep->set_status(seastar::http::reply::status_type::bad_request);
                rep->write_body("application/json",
                                json{{"error", std::string("Champs texte invalides: ") + e.what()}}.dump());
                co_return std::move(rep);
            }
        } else {
            // Mode JSON classique
            record = parser.parse(*entity, body);
        }

        // ─── Hash du password si present ─────────────────────
        // (Avant l'upload des fichiers car ça peut faire échouer
        // rapidement et éviter des écritures disque inutiles.)
        const auto password_it = record.find("password");
        if (password_it != record.end()) {
            const auto plain_password = sea::http::utils::dynamic_value_to_string(password_it->second);
            if (!plain_password.has_value()) {
                rep->set_status(seastar::http::reply::status_type::bad_request);
                rep->write_body("application/json", json{{"error", "Password invalide."}}.dump());
                co_return std::move(rep);
            }

            const auto hashed_password =
                co_await blocking_executor_->submit(
                    [auth_service = auth_service_,
                     plain = *plain_password] {
                        return auth_service->hash_password(plain);
                    }
                    );

            record["password"] = hashed_password;

            if (record.find("role") == record.end()) {
                record["role"] = std::string("user");
            }
        }

        // ─── Check ABAC ──────────────────────────────────────
        // Inchangé. Pour les champs File, l'ABAC voit la valeur
        // courante (UUID si déjà fourni, ou rien si multipart sans
        // upload encore). Les règles ABAC sur champ File devraient
        // donc tester l'existence (exists) plutôt que la valeur
        // précise.
        if (auth_helper_) {
            nlohmann::json payload_json = nlohmann::json::object();
            for (const auto& [key, value] : record) {
                if (key == "password") {
                    continue;
                }
                const auto str_value = sea::http::utils::dynamic_value_to_string(value);
                if (str_value.has_value()) {
                    payload_json[key] = *str_value;
                }
            }

            const std::string payload_str = payload_json.dump();
            const auto subject = auth_helper_->build_subject_from_headers(*req);
            const std::string path_str(req->_url.data(), req->_url.size());
            const auto context = auth_helper_->build_context(*req, path_str);

            const auto check = auth_helper_->check_single(
                entity_name_,
                sea::domain::access_control::CrudOperation::Create,
                subject,
                payload_str,
                context
                );

            if (!check.allowed) {
                rep->set_status(seastar::http::reply::status_type::forbidden);
                rep->write_body("application/json",
                                json{
                                    {"error", "Forbidden"},
                                    {"message", check.reason}
                                }.dump());
                co_return std::move(rep);
            }
        }

        // ─── Génération de l'ID ──────────────────────────────
        // Inchangé.
        const sea::domain::Field* id_field = nullptr;
        for (const auto& field : entity->fields) {
            if (field.name == "id") {
                id_field = &field;
                break;
            }
        }

        if (id_field != nullptr) {
            if (id_field->type == sea::domain::FieldType::UUID) {
                std::string new_id;
                do {
                    new_id = sea::http::utils::generate_uuid();
                } while ((co_await crud_engine_->get_by_id(entity_name_, new_id)).has_value());

                record["id"] = new_id;
            } else if (id_field->type == sea::domain::FieldType::Int) {
                record["id"] = co_await sea::http::utils::generate_int_id(entity_name_, crud_engine_);
            }
        }

        // ─── Transaction : upload files + create + retain ───
        //
        // Toutes les écritures DB sont dans la même tx pour atomicité.
        // Les écritures DISQUE (storage->store) sont hors tx par nature
        // (le filesystem n'est pas transactionnel). En cas de rollback
        // SQL, on supprime explicitement les fichiers physiques via
        // extractor_->rollback().
        sea::http::handlers::file_upload::ExtractionResult extraction{};
        sea::infrastructure::runtime::GenericCrudEngine::OperationResult op_result{};

        auto tx = co_await crud_engine_->get_repository()->in_transaction(
            [this, entity, &record, &extraction, &op_result,
             is_multipart, &parsed_multipart]() -> seastar::future<bool>
            {
                // 1. Upload des fichiers
                if (file_extractor_ != nullptr) {
                    try {
                        if (is_multipart) {
                            // On a déjà parsé le multipart plus haut.
                            // Boucle l'upload manuellement avec les file parts.
                            // (extract_from_multipart re-parse — on évite ça en
                            // utilisant les file_parts déjà disponibles.)
                            //
                            // Pour rester simple et utiliser la méthode existante,
                            // on appelle juste extract_from_json_record() qui ne
                            // fait rien si pas de champs JSON, puis on fait l'upload
                            // ici via une boucle directe.
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
                            // Mode JSON : laisse l'extractor traiter les
                            // objets {filename, mime_type, content_base64}.
                            extraction = co_await file_extractor_->extract_from_json_record(
                                *entity, record);
                        }
                    } catch (const std::exception& e) {
                        spdlog::get("sea.http")->error(
                            "CreateHandler: file extraction failed: {}", e.what());
                        co_return false;
                    }
                }

                // 2. INSERT entité
                op_result = co_await crud_engine_->create(entity_name_, record);
                if (!op_result.success) {
                    co_return false;
                }

                // 3. retain UUIDs (UPDATE sea_files SET ref_count = ref_count + 1)
                if (extraction.had_files) {
                    const bool retain_ok = co_await file_extractor_->commit(extraction);
                    if (!retain_ok) {
                        co_return false;
                    }
                }

                co_return true;
            });

        if (!tx.committed) {
            // Rollback explicite des fichiers physiques (le SQL a déjà rollback,
            // mais les fichiers sur disque ont été créés avant le rollback).
            if (extraction.had_files) {
                co_await file_extractor_->rollback(extraction);
            }

            // Construit la réponse d'erreur
            if (!op_result.success && !op_result.errors.empty()) {
                std::ostringstream oss;
                oss << "{ \"errors\": [";
                for (std::size_t i = 0; i < op_result.errors.size(); ++i) {
                    if (i != 0) oss << ",";
                    oss << "\"" << sea::http::utils::json_escape(op_result.errors[i]) << "\"";
                }
                oss << "] }";
                rep->set_status(seastar::http::reply::status_type::bad_request);
                rep->write_body("application/json", oss.str());
            } else {
                rep->set_status(seastar::http::reply::status_type::internal_server_error);
                rep->write_body("application/json",
                                json{{"error", "Transaction echouee."}}.dump());
            }
            co_return std::move(rep);
        }

        rep->set_status(seastar::http::reply::status_type::created);
        rep->write_body("application/json", sea::http::utils::record_to_json(*op_result.record));
        co_return std::move(rep);

    } catch (const std::exception& e) {
        rep->set_status(seastar::http::reply::status_type::bad_request);
        rep->write_body("application/json", json{{"error", std::string("Erreur: ") + e.what()}}.dump());
        co_return std::move(rep);
    }
}

} // namespace sea::http::handlers::crud