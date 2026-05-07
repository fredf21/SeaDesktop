#include "create_handler.h"
#include "../access_control/resource_authorization_helper.h"
#include "../../utils/http_utils.h"

#include "authservice.h"
#include "access_control/crud_operation.h"
#include "runtime/generic_crud_engine.h"
#include "runtime/json_record_parser.h"
#include "runtime/schema_runtime_registry.h"

#include <nlohmann/json.hpp>
#include <seastar/core/thread.hh>
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
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper)
    : crud_engine_(std::move(crud_engine))
    , registry_(std::move(registry))
    , entity_name_(std::move(entity_name))
    , auth_service_(std::move(auth_service))
    , db_type_(db_type)
    , blocking_executor_(std::move(blocking_executor))
    , auth_helper_(std::move(auth_helper))
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

    try {
        const std::string body = co_await sea::http::utils::read_request_body(*req);

        sea::infrastructure::runtime::JsonRecordParser parser;
        auto record = parser.parse(*entity, body);

        // Hash du password si present (avant le check ABAC car le password
        // peut faire partie de la regle, mais surtout pour eviter de hasher
        // si on rejette ensuite)
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

        // ✨ Module 6 : check ABAC sur le PAYLOAD (avant l'INSERT)
        // Pour Create, la "ressource" est le payload entrant car aucune
        // ressource n'existe encore en DB. Cela permet par exemple de
        // bloquer un manager IT qui essaie de creer un employe HR via
        // la regle same_scope.
        if (auth_helper_) {
            // Construit un JSON depuis le record pour le helper
            nlohmann::json payload_json = nlohmann::json::object();
            for (const auto& [key, value] : record) {
                // Exclure le password (jamais dans les attributes ABAC)
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

        // ✨ FIX : Generation ID pour TOUS les types de DB (pas seulement Memory)
        // Bug pre-existant : avant on ne generait l'UUID que pour Memory,
        // ce qui faisait planter MySQL avec "Incorrect string value" car
        // record["id"] etait absent.
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

        const auto result = co_await crud_engine_->create(entity_name_, std::move(record));
        if (!result.success || !result.record.has_value()) {
            std::ostringstream oss;
            oss << "{ \"errors\": [";

            for (std::size_t i = 0; i < result.errors.size(); ++i) {
                if (i != 0) {
                    oss << ",";
                }
                oss << "\"" << sea::http::utils::json_escape(result.errors[i]) << "\"";
            }

            oss << "] }";

            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("application/json", oss.str());
            co_return std::move(rep);
        }

        rep->set_status(seastar::http::reply::status_type::created);
        rep->write_body("application/json", sea::http::utils::record_to_json(*result.record));
        co_return std::move(rep);

    } catch (const std::exception& e) {
        rep->set_status(seastar::http::reply::status_type::bad_request);
        rep->write_body("application/json", json{{"error", std::string("Erreur JSON: ") + e.what()}}.dump());
        co_return std::move(rep);
    }
}

} // namespace sea::http::handlers::crud