#include "register_handler.h"
#include "../../utils/http_utils.h"
#include "../../errors/error_response_factory.h"

#include "authservice.h"
#include "database_config.h"
#include "runtime/generic_crud_engine.h"
#include "runtime/json_record_parser.h"
#include "runtime/schema_runtime_registry.h"
#include "spdlog/spdlog.h"

#include <nlohmann/json.hpp>
#include <utility>

namespace sea::http::handlers::auth {

using json = nlohmann::json;
namespace errors = sea::http::errors;
using Status = seastar::http::reply::status_type;

RegisterHandler::RegisterHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry> registry,
    std::shared_ptr<sea::application::AuthService> auth_service,
    std::shared_ptr<IBlockingExecutor> blocking_executor,
    sea::domain::DatabaseType db_type)
    : crud_engine_(std::move(crud_engine))
    , registry_(std::move(registry))
    , auth_service_(std::move(auth_service))
    , blocking_executor_(std::move(blocking_executor))
    , db_type_(db_type)
{
}

/**
 * RegisterHandler
 *
 * Étapes :
 * 1. Parser le body JSON
 * 2. Vérifier email unique
 * 3. Hasher le password (hors reactor)
 * 4. Générer ID si nécessaire
 * 5. Créer l'utilisateur
 */
seastar::future<std::unique_ptr<seastar::http::reply>>
RegisterHandler::handle(const seastar::sstring&,
                        std::unique_ptr<seastar::http::request> req,
                        std::unique_ptr<seastar::http::reply> rep)
{
    const auto* entity = registry_->find_entity("User");

    if (entity == nullptr) {
        co_return errors::make_error_reply(
            Status::not_found, "NOT_FOUND",
            "Entite User introuvable.");
    }

    try {
        // Lecture du body
        const std::string body =
            co_await sea::http::utils::read_request_body(*req);

        // Parsing dynamique
        sea::infrastructure::runtime::JsonRecordParser parser;
        auto record = parser.parse(*entity, body);

        // Validation email
        const auto email_it = record.find("email");
        if (email_it == record.end()) {
            co_return errors::make_error_reply(
                Status::bad_request, "VALIDATION_ERROR",
                "Champ email manquant.");
        }

        const auto email =
            sea::http::utils::dynamic_value_to_string(email_it->second);

        if (!email.has_value()) {
            co_return errors::make_error_reply(
                Status::bad_request, "VALIDATION_ERROR",
                "Email invalide.");
        }

        // Vérifie unicité email
        const auto existing_user =
            co_await crud_engine_->find_one_by_field("User", "email", *email);

        if (existing_user.has_value()) {
            co_return errors::make_error_reply(
                Status::conflict, "CONFLICT",
                "Cet email existe deja.");
        }

        // Validation password
        const auto password_it = record.find("password");
        if (password_it == record.end()) {
            co_return errors::make_error_reply(
                Status::bad_request, "VALIDATION_ERROR",
                "Champ password manquant.");
        }

        const auto plain_password =
            sea::http::utils::dynamic_value_to_string(password_it->second);

        if (!plain_password.has_value()) {
            co_return errors::make_error_reply(
                Status::bad_request, "VALIDATION_ERROR",
                "Password invalide.");
        }

        /**
         *  Hash du mot de passe hors reactor
         *
         * Avant :
         *   seastar::async(...)
         *
         * Maintenant :
         *   blocking_executor_->submit(...)
         *
         * Pourquoi ?
         * bcrypt / argon2 = CPU heavy → ne doit jamais tourner dans reactor
         */
        const auto hashed_password =
            co_await blocking_executor_->submit(
                [auth_service = auth_service_,
                 plain = *plain_password] {
                    return auth_service->hash_password(plain);
                }
                );

        record["password"] = hashed_password;

        //  Role par défaut
        if (record.find("role") == record.end()) {
            record["role"] = std::string("user");
        }

        /**
         *  Génération ID
         */

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
                } while ((co_await crud_engine_->get_by_id("User", new_id)).has_value());

                record["id"] = new_id;
            } else if (id_field->type == sea::domain::FieldType::Int) {
                record["id"] =
                    co_await sea::http::utils::generate_int_id("User", crud_engine_);
            }
        }
        auto http_log = spdlog::get("sea.http");
        if (http_log->should_log(spdlog::level::debug)) {
            http_log->debug("Record contents before create (User):");
            for (const auto& [key, value] : record) {
                if (auto str = sea::http::utils::dynamic_value_to_string(value)) {
                    http_log->debug("  - {} : '{}' (len={})", key, *str, str->size());
                } else {
                    http_log->debug("  - {} : <not a string>", key);
                }
            }
        }

        // Création utilisateur
        const auto result =
            co_await crud_engine_->create("User", std::move(record));

        if (!result.success || !result.record.has_value()) {
            // Concatene les erreurs metier si presentes.
            if (!result.errors.empty()) {
                std::string combined;
                for (std::size_t i = 0; i < result.errors.size(); ++i) {
                    if (i != 0) combined += "; ";
                    combined += result.errors[i];
                }
                co_return errors::make_error_reply(
                    Status::bad_request, "VALIDATION_ERROR", combined);
            }
            // Echec sans message d'erreur explicite : cas suspect.
            spdlog::get("sea.http")->error(
                "RegisterHandler: create('User') failed sans message d'erreur "
                "(email='{}')", *email);
            co_return errors::make_internal_error_reply();
        }

        // Nettoyage réponse
        json user_json =
            json::parse(sea::http::utils::record_to_json(*result.record));

        user_json.erase("password");

        rep->set_status(Status::created);
        rep->write_body("application/json", user_json.dump());

        co_return std::move(rep);

    } catch (const errors::HttpException& e) {
        // Erreur metier deja typee : on respecte son statut.
        co_return errors::make_error_reply(e);
    } catch (const std::exception& e) {
        // Erreur generique : probable JSON malforme ou parsing.
        spdlog::get("sea.http")->warn(
            "RegisterHandler: unhandled exception: {}", e.what());
        co_return errors::make_error_reply(
            Status::bad_request, "BAD_REQUEST",
            std::string("Erreur register: ") + e.what());
    }
}

} // namespace