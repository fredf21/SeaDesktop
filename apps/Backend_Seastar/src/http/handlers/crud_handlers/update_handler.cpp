#include "update_handler.h"
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

UpdateHandler::UpdateHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry> registry,
    std::shared_ptr<sea::application::AuthService> auth_service,
    std::string entity_name,
    std::shared_ptr<IBlockingExecutor> blocking_executor,
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper)
    : crud_engine_(std::move(crud_engine))
    , registry_(std::move(registry))
    , auth_service_(std::move(auth_service))
    , entity_name_(std::move(entity_name))
    , blocking_executor_(std::move(blocking_executor))
    , auth_helper_(std::move(auth_helper))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
UpdateHandler::handle(const seastar::sstring&,
                      std::unique_ptr<seastar::http::request> req,
                      std::unique_ptr<seastar::http::reply> rep)
{
    const auto id = req->get_path_param("id");
    if (id.empty()) {
        rep->set_status(seastar::http::reply::status_type::bad_request);
        rep->write_body("application/json", json{{"error", "Parametre 'id' manquant."}}.dump());
        co_return std::move(rep);
    }

    const auto* entity = registry_->find_entity(entity_name_);
    if (entity == nullptr) {
        rep->set_status(seastar::http::reply::status_type::not_found);
        rep->write_body("application/json", json{{"error", "Entite inconnue."}}.dump());
        co_return std::move(rep);
    }

    try {
        // check ABAC AVANT modification
        // On charge la ressource existante pour evaluer les regles ABAC
        // (own_resource, same_scope, etc.) sur l'etat AVANT modification.
        if (auth_helper_) {
            const auto current = co_await crud_engine_->get_by_id(
                entity_name_, std::string(id)
                );

            if (!current.has_value()) {
                rep->set_status(seastar::http::reply::status_type::not_found);
                rep->write_body("application/json",
                                json{{"error", "Enregistrement introuvable."}}.dump());
                co_return std::move(rep);
            }

            const std::string current_json = sea::http::utils::record_to_json(*current);

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
                rep->set_status(seastar::http::reply::status_type::forbidden);
                rep->write_body("application/json",
                                json{
                                    {"error", "Forbidden"},
                                    {"message", check.reason}
                                }.dump());
                co_return std::move(rep);
            }
        }

        const std::string body = co_await sea::http::utils::read_request_body(*req);

        sea::infrastructure::runtime::JsonRecordParser parser;
        auto record = parser.parse(*entity, body);

        const auto password_it = record.find("password");
        if (password_it != record.end()) {
            const auto plain_password = sea::http::utils::dynamic_value_to_string(password_it->second);
            if (!plain_password.has_value()) {
                rep->set_status(seastar::http::reply::status_type::bad_request);
                rep->write_body("application/json", json{{"error", "Password invalide."}}.dump());
                co_return std::move(rep);
            }

            const auto hashed_password = co_await blocking_executor_->submit(
                [auth_service = auth_service_,
                 plain = *plain_password] {
                    return auth_service->hash_password(plain);
                }
                );
            record["password"] = hashed_password;
        }

        const auto result = co_await crud_engine_->update(entity_name_, std::string(id), std::move(record));
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

        rep->set_status(seastar::http::reply::status_type::ok);
        rep->write_body("application/json", sea::http::utils::record_to_json(*result.record));
        co_return std::move(rep);

    } catch (const std::exception& e) {
        rep->set_status(seastar::http::reply::status_type::bad_request);
        rep->write_body("application/json", json{{"error", std::string("Erreur JSON: ") + e.what()}}.dump());
        co_return std::move(rep);
    }
}

} // namespace sea::http::handlers::crud