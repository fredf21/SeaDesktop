#include "register_handler.h"
#include "../../utils/http_utils.h"

#include "authservice.h"
#include "database_config.h"
#include "runtime/generic_crud_engine.h"
#include "runtime/json_record_parser.h"
#include "runtime/schema_runtime_registry.h"

#include <nlohmann/json.hpp>
#include <utility>

namespace sea::http::handlers::auth {

using json = nlohmann::json;

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
        rep->set_status(seastar::http::reply::status_type::not_found);
        rep->write_body("application/json",
                        json{{"error", "Entite User introuvable."}}.dump());
        co_return std::move(rep);
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
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("application/json",
                            json{{"error", "Champ email manquant."}}.dump());
            co_return std::move(rep);
        }

        const auto email =
            sea::http::utils::dynamic_value_to_string(email_it->second);

        if (!email.has_value()) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("application/json",
                            json{{"error", "Email invalide."}}.dump());
            co_return std::move(rep);
        }

        // Vérifie unicité email
        const auto existing_user =
            co_await crud_engine_->find_one_by_field("User", "email", *email);

        if (existing_user.has_value()) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("application/json",
                            json{{"error", "Cet email existe deja."}}.dump());
            co_return std::move(rep);
        }

        // Validation password
        const auto password_it = record.find("password");
        if (password_it == record.end()) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("application/json",
                            json{{"error", "Champ password manquant."}}.dump());
            co_return std::move(rep);
        }

        const auto plain_password =
            sea::http::utils::dynamic_value_to_string(password_it->second);

        if (!plain_password.has_value()) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("application/json",
                            json{{"error", "Password invalide."}}.dump());
            co_return std::move(rep);
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
         *  Génération ID pour InMemory
         */
        if (db_type_ == sea::domain::DatabaseType::Memory) {
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
        }

        // Création utilisateur
        const auto result =
            co_await crud_engine_->create("User", std::move(record));

        if (!result.success || !result.record.has_value()) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("application/json",
                            json{{"error", "Impossible de creer l'utilisateur."}}.dump());
            co_return std::move(rep);
        }

        // Nettoyage réponse
        json user_json =
            json::parse(sea::http::utils::record_to_json(*result.record));

        user_json.erase("password");

        rep->set_status(seastar::http::reply::status_type::created);
        rep->write_body("application/json", user_json.dump());

        co_return std::move(rep);

    } catch (const std::exception& e) {
        rep->set_status(seastar::http::reply::status_type::bad_request);
        rep->write_body("application/json",
                        json{{"error", std::string("Erreur register: ") + e.what()}}.dump());
        co_return std::move(rep);
    }
}

} // namespace