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
    sea::domain::DatabaseType db_type)
    : crud_engine_(std::move(crud_engine))
    , registry_(std::move(registry))
    , auth_service_(std::move(auth_service))
    , db_type_(db_type)
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
RegisterHandler::handle(const seastar::sstring&,
                        std::unique_ptr<seastar::http::request> req,
                        std::unique_ptr<seastar::http::reply> rep)
{
    const auto* entity = registry_->find_entity("User");
    if (entity == nullptr) {
        rep->set_status(seastar::http::reply::status_type::not_found);
        rep->write_body("application/json", json{{"error", "Entite User introuvable."}}.dump());
        co_return std::move(rep);
    }

    try {

        // std::cerr << "[REGISTER] start\n";

        const std::string body = co_await sea::http::utils::read_request_body(*req);

        // std::cerr << "[REGISTER] body = " << body << "\n";

        sea::infrastructure::runtime::JsonRecordParser parser;
        auto record = parser.parse(*entity, body);
        // std::cerr << "[REGISTER] parsed\n";
        // std::cerr << "[REGISTER] before list\n";
        const auto all_users = co_await crud_engine_->list("User");
        // std::cerr << "[REGISTER] users loaded: " << all_users.size() << "\n";
        const auto email_it = record.find("email");
        if (email_it == record.end()) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("application/json", json{{"error", "Champ email manquant."}}.dump());
            co_return std::move(rep);
        }

        const auto email = sea::http::utils::dynamic_value_to_string(email_it->second);
        if (!email.has_value()) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("application/json", json{{"error", "Email invalide."}}.dump());
            co_return std::move(rep);
        }

        if (sea::http::utils::find_record_by_field(all_users, "email", *email).has_value()) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("application/json", json{{"error", "Cet email existe deja."}}.dump());
            co_return std::move(rep);
        }

        const auto password_it = record.find("password");
        if (password_it == record.end()) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("application/json", json{{"error", "Champ password manquant."}}.dump());
            co_return std::move(rep);
        }

        const auto plain_password = sea::http::utils::dynamic_value_to_string(password_it->second);
        if (!plain_password.has_value()) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("application/json", json{{"error", "Password invalide."}}.dump());
            co_return std::move(rep);
        }

        record["password"] = auth_service_->hash_password(*plain_password);

        if (record.find("role") == record.end()) {
            record["role"] = std::string("user");
        }

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
                    record["id"] = co_await sea::http::utils::generate_int_id("User", crud_engine_);
                }
            }
        }

        const auto result = co_await crud_engine_->create("User", std::move(record));
        // std::cerr << "[REGISTER] create finished success=" << result.success << "\n";
        if (!result.success || !result.record.has_value()) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("application/json", json{{"error", "Impossible de creer l'utilisateur."}}.dump());
            co_return std::move(rep);
        }

        json user_json = json::parse(sea::http::utils::record_to_json(*result.record));
        user_json.erase("password");

        rep->set_status(seastar::http::reply::status_type::created);
        rep->write_body("application/json", user_json.dump());
        co_return std::move(rep);

    } catch (const std::exception& e) {
        rep->set_status(seastar::http::reply::status_type::bad_request);
        rep->write_body("application/json", json{{"error", std::string("Erreur register: ") + e.what()}}.dump());
        co_return std::move(rep);
    }
}

} // namespace sea::http::handlers::auth
