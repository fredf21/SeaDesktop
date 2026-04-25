#include "login_handler.h"
#include "../../utils/http_utils.h"

#include "authservice.h"
#include "runtime/generic_crud_engine.h"

#include <nlohmann/json.hpp>
#include <seastar/core/thread.hh>
#include <utility>

namespace sea::http::handlers::auth {

using json = nlohmann::json;

LoginHandler::LoginHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::shared_ptr<sea::application::AuthService> auth_service)
    : crud_engine_(std::move(crud_engine))
    , auth_service_(std::move(auth_service))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
LoginHandler::handle(const seastar::sstring&,
                     std::unique_ptr<seastar::http::request> req,
                     std::unique_ptr<seastar::http::reply> rep)
{
    try {
        const std::string reqbody = co_await sea::http::utils::read_request_body(*req);
        const auto body = json::parse(reqbody);

        if (!body.contains("email") || !body.contains("password")) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("application/json",
                            json{{"error", "email et password sont requis."}}.dump());
            co_return std::move(rep);
        }

        const auto email = body["email"].get<std::string>();
        const auto password = body["password"].get<std::string>();

        const auto users = co_await crud_engine_->list("User");
        const auto user_record =
            sea::http::utils::find_record_by_field(users, "email", email);

        if (!user_record.has_value()) {
            rep->set_status(seastar::http::reply::status_type::unauthorized);
            rep->write_body("application/json",
                            json{{"error", "Identifiants invalides."}}.dump());
            co_return std::move(rep);
        }

        const auto pwd_it = user_record->find("password");
        if (pwd_it == user_record->end()) {
            rep->set_status(seastar::http::reply::status_type::unauthorized);
            rep->write_body("application/json",
                            json{{"error", "Identifiants invalides."}}.dump());
            co_return std::move(rep);
        }

        const auto stored_hash =
            sea::http::utils::dynamic_value_to_string(pwd_it->second);

        bool password_ok = false;
        if (stored_hash.has_value()) {
            password_ok = co_await seastar::async(
                [this, &password, &hash = *stored_hash] {
                    return auth_service_->verify_password(password, hash);
                });
        }

        if (!password_ok) {
            rep->set_status(seastar::http::reply::status_type::unauthorized);
            rep->write_body("application/json",
                            json{{"error", "Identifiants invalides."}}.dump());
            co_return std::move(rep);
        }

        const auto id_it = user_record->find("id");
        if (id_it == user_record->end()) {
            rep->set_status(seastar::http::reply::status_type::internal_server_error);
            rep->write_body("application/json",
                            json{{"error", "Utilisateur sans id."}}.dump());
            co_return std::move(rep);
        }

        const auto user_id =
            sea::http::utils::dynamic_value_to_string_id(id_it->second);

        if (!user_id.has_value()) {
            rep->set_status(seastar::http::reply::status_type::internal_server_error);
            rep->write_body("application/json",
                            json{{"error", "ID utilisateur invalide."}}.dump());
            co_return std::move(rep);
        }

        std::string role = "user";
        const auto role_it = user_record->find("role");
        if (role_it != user_record->end()) {
            const auto role_value =
                sea::http::utils::dynamic_value_to_string(role_it->second);

            if (role_value.has_value() && !role_value->empty()) {
                role = *role_value;
            }
        }

        const auto token =
            auth_service_->generate_token(*user_id, email, role);

        json user_json =
            json::parse(sea::http::utils::record_to_json(*user_record));

        user_json.erase("password");

        rep->set_status(seastar::http::reply::status_type::ok);
        rep->write_body("application/json",
                        json{
                            {"token", token},
                            {"user", user_json}
                        }.dump());

        co_return std::move(rep);

    } catch (const std::exception& e) {
        rep->set_status(seastar::http::reply::status_type::bad_request);
        rep->write_body("application/json",
                        json{{"error", std::string("Erreur login: ") + e.what()}}.dump());
        co_return std::move(rep);
    }
}
} // namespace sea::http::handlers::auth
