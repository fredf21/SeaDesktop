#include "login_handler.h"
#include "../../utils/http_utils.h"

#include "authservice.h"
#include "runtime/generic_crud_engine.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace sea::http::handlers::auth {

using json = nlohmann::json;

LoginHandler::LoginHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::shared_ptr<sea::application::AuthService> auth_service,
    std::shared_ptr<IBlockingExecutor> blocking_executor)
    : crud_engine_(std::move(crud_engine))
    , auth_service_(std::move(auth_service))
    , blocking_executor_(std::move(blocking_executor))
{
}

/**
 * LoginHandler
 *
 * Route : POST /auth/login
 *
 * Flow :
 * 1. Lire le body HTTP
 * 2. Parser JSON
 * 3. Vérifier email/password
 * 4. Récupérer utilisateur
 * 5. Vérifier password (thread pool)
 * 6. Générer tokens JWT
 * 7. Retourner réponse
 */
seastar::future<std::unique_ptr<seastar::http::reply>>
LoginHandler::handle(const seastar::sstring&,
                     std::unique_ptr<seastar::http::request> req,
                     std::unique_ptr<seastar::http::reply> rep)
{
    try {
        // Lecture body HTTP
        const std::string reqbody =
            co_await sea::http::utils::read_request_body(*req);

        // Parsing JSON
        const auto body = json::parse(reqbody);

        // Validation input
        if (!body.contains("email") || !body.contains("password")) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("application/json",
                            json{{"error", "email et password sont requis"}}.dump());
            co_return std::move(rep);
        }

        const std::string email = body["email"].get<std::string>();
        const std::string password = body["password"].get<std::string>();

        // 🔹 Recherche utilisateur
        const auto user_record =
            co_await crud_engine_->find_one_by_field("User", "email", email);

        if (!user_record.has_value()) {
            // volontairement vague (sécurité)
            rep->set_status(seastar::http::reply::status_type::unauthorized);
            rep->write_body("application/json",
                            json{{"error", "Identifiants invalides"}}.dump());
            co_return std::move(rep);
        }

        // 🔹 Récupération hash password
        const auto pwd_it = user_record->find("password");
        if (pwd_it == user_record->end()) {
            rep->set_status(seastar::http::reply::status_type::unauthorized);
            rep->write_body("application/json",
                            json{{"error", "Identifiants invalides"}}.dump());
            co_return std::move(rep);
        }

        const auto stored_hash =
            sea::http::utils::dynamic_value_to_string(pwd_it->second);

        if (!stored_hash.has_value() || stored_hash->empty()) {
            rep->set_status(seastar::http::reply::status_type::unauthorized);
            rep->write_body("application/json",
                            json{{"error", "Identifiants invalides"}}.dump());
            co_return std::move(rep);
        }

        /**
         * Vérification password hors reactor
         *
         * Très important :
         * - bcrypt / argon2 = CPU heavy
         * - ne doit JAMAIS bloquer Seastar
         */
        const bool password_ok =
            co_await blocking_executor_->submit(
                [auth_service = auth_service_,
                 password,
                 hash = *stored_hash]() {
                    return auth_service->verify_password(password, hash);
                }
                );

        if (!password_ok) {
            rep->set_status(seastar::http::reply::status_type::unauthorized);
            rep->write_body("application/json",
                            json{{"error", "Identifiants invalides"}}.dump());
            co_return std::move(rep);
        }

        // ID utilisateur
        const auto id_it = user_record->find("id");
        if (id_it == user_record->end()) {
            rep->set_status(seastar::http::reply::status_type::internal_server_error);
            rep->write_body("application/json",
                            json{{"error", "Utilisateur sans id"}}.dump());
            co_return std::move(rep);
        }

        const auto user_id =
            sea::http::utils::dynamic_value_to_string_id(id_it->second);

        if (!user_id.has_value()) {
            rep->set_status(seastar::http::reply::status_type::internal_server_error);
            rep->write_body("application/json",
                            json{{"error", "ID utilisateur invalide"}}.dump());
            co_return std::move(rep);
        }

        //  Role (fallback user)
        std::string role = "user";

        const auto role_it = user_record->find("role");
        if (role_it != user_record->end()) {
            const auto role_value =
                sea::http::utils::dynamic_value_to_string(role_it->second);

            if (role_value.has_value() && !role_value->empty()) {
                role = *role_value;
            }
        }

        //  Génération JWT
        const auto access_token =
            auth_service_->generate_access_token(*user_id, email, role);

        const auto refresh_token =
            auth_service_->generate_refresh_token(*user_id);

        /**
         *  Nettoyage user
         *
         * Important : ne jamais renvoyer le password
         */
        json user_json =
            json::parse(sea::http::utils::record_to_json(*user_record));

        user_json.erase("password");

        rep->set_status(seastar::http::reply::status_type::ok);
        rep->write_body("application/json",
                        json{
                            {"access_token", access_token},
                            {"refresh_token", refresh_token},
                            {"token_type", "Bearer"},
                            {"user", user_json}
                        }.dump()
                        );

        co_return std::move(rep);

    } catch (...) {
        /**
         *️ Important en prod :
         * ne pas exposer les erreurs internes
         */
        rep->set_status(seastar::http::reply::status_type::bad_request);
        rep->write_body("application/json",
                        json{{"error", "Requete invalide"}}.dump());

        co_return std::move(rep);
    }
}

} // namespace