#include <seastar/core/app-template.hh>
#include <seastar/core/sleep.hh>
#include <seastar/http/handlers.hh>
#include <seastar/http/httpd.hh>

#include <chrono>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <variant>
#include <nlohmann/json.hpp>
#include <random>
#include <iomanip>
// Application
#include "authservice.h"
#include "import_yaml_schema_usecase.h"
#include "openapigenerator.h"
#include "route_generator.h"
#include "start_service_usecase.h"
#include "validate_schema_usecase.h"

// Infrastructure
#include "persistence/repository_factory.h"
#include "runtime/generic_crud_engine.h"
#include "runtime/generic_validator.h"
#include "runtime/json_record_parser.h"
#include "runtime/schema_runtime_registry.h"

// Boost program options
#include <boost/program_options.hpp>
namespace bpo = boost::program_options;

using json = nlohmann::json;

namespace {

using sea::infrastructure::runtime::DynamicRecord;
using sea::infrastructure::runtime::DynamicValue;
using sea::infrastructure::runtime::GenericCrudEngine;

constexpr bool kEnableCrudRoutes = true;

// ─────────────────────────────────────────────────────────────
// Helpers JSON
// ─────────────────────────────────────────────────────────────

/**
 * @brief Échappe les caractères spéciaux pour JSON.
 *
 * Cette fonction transforme une chaîne brute en une version compatible JSON
 * en échappant les caractères spéciaux (", \, \n, etc.).
 *
 * @param input Chaîne à échapper.
 *
 * @return std::string Chaîne échappée.
 *
 * @note Nécessaire pour éviter des JSON invalides.
 */
[[nodiscard]] std::string json_escape(const std::string& input) {
    std::string out;
    out.reserve(input.size());

    for (char c : input) {
        switch (c) {
        case '\"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:   out += c; break;
        }
    }

    return out;
}



/**
 * @brief Convertit un DynamicValue en représentation JSON.
 *
 * Cette fonction transforme une valeur dynamique en chaîne JSON valide.
 *
 * @param value Valeur dynamique.
 *
 * @return std::string Représentation JSON.
 *
 * @note Types supportés :
 * - null (std::monostate)
 * - string
 * - int64
 * - double
 * - bool
 */
[[nodiscard]] std::string dynamic_value_to_json(const DynamicValue& value) {
    if (std::holds_alternative<std::monostate>(value)) {
        return "null";
    }

    if (std::holds_alternative<std::string>(value)) {
        return "\"" + json_escape(std::get<std::string>(value)) + "\"";
    }

    if (std::holds_alternative<std::int64_t>(value)) {
        return std::to_string(std::get<std::int64_t>(value));
    }

    if (std::holds_alternative<double>(value)) {
        std::ostringstream oss;
        oss << std::get<double>(value);
        return oss.str();
    }

    if (std::holds_alternative<bool>(value)) {
        return std::get<bool>(value) ? "true" : "false";
    }

    return "null";
}

/**
 * @brief Convertit un enregistrement dynamique en JSON.
 *
 * Cette fonction transforme un DynamicRecord (clé-valeur)
 * en objet JSON sous forme de chaîne.
 *
 * @param record Enregistrement dynamique.
 *
 * @return std::string JSON représentant l’objet.
 *
 * @example
 * { "id": 1, "name": "Alice" }
 */
[[nodiscard]] std::string record_to_json(const DynamicRecord& record) {
    std::ostringstream oss;
    oss << "{";

    bool first = true;
    for (const auto& [key, value] : record) {
        if (!first) {
            oss << ",";
        }
        first = false;
        oss << "\"" << json_escape(key) << "\":" << dynamic_value_to_json(value);
    }

    oss << "}";
    return oss.str();
}


/**
 * @brief Convertit une liste d'enregistrements en tableau JSON.
 *
 * Cette fonction transforme un vecteur de DynamicRecord
 * en un tableau JSON.
 *
 * @param records Liste d'enregistrements.
 *
 * @return std::string JSON représentant un tableau.
 *
 * @example
 * [
 *   { "id": 1 },
 *   { "id": 2 }
 * ]
 */
[[nodiscard]] std::string records_to_json(const std::vector<DynamicRecord>& records) {
    std::ostringstream oss;
    oss << "[";

    bool first = true;
    for (const auto& record : records) {
        if (!first) {
            oss << ",";
        }
        first = false;
        oss << record_to_json(record);
    }

    oss << "]";
    return oss.str();
}
// ─────────────────────────────────────────────────────────────
// Genreration automatique du id si le type de la base de donnee est memory
// ─────────────────────────────────────────────────────────────


/**
 * @brief Génère un identifiant entier auto-incrémenté.
 *
 * Cette fonction parcourt les enregistrements existants d'une entité
 * et retourne un identifiant égal au maximum trouvé + 1.
 *
 * @param entity_name Nom de l'entité.
 * @param crud_engine Moteur CRUD utilisé pour récupérer les données existantes.
 *
 * @return std::int64_t Nouvel identifiant unique.
 *
 * @note Utilisé principalement pour les bases de données en mémoire.
 */
[[nodiscard]] std::int64_t generate_int_id(
    const std::string& entity_name,
    std::shared_ptr<GenericCrudEngine> crud_engine)
{
    const auto records = crud_engine->list(entity_name);
    std::int64_t max_id = 0;
    for (const auto& record : records) {
        auto it = record.find("id");
        if (it != record.end() && std::holds_alternative<std::int64_t>(it->second)) {
            max_id = std::max(max_id, std::get<std::int64_t>(it->second));
        }
    }
    return max_id + 1;
}


/**
 * @brief Génère un identifiant UUID version 4.
 *
 * Cette fonction crée un UUID pseudo-aléatoire conforme au format standard :
 * xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
 *
 * @return std::string UUID généré.
 *
 * @note
 * - Utilise std::mt19937_64 pour la génération aléatoire.
 * - Ne garantit pas l’unicité absolue (doit être vérifiée côté appelant).
 */
[[nodiscard]] std::string generate_uuid() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;

    uint64_t part1 = dis(gen);
    uint64_t part2 = dis(gen);

    part1 = (part1 & 0xffffffffffff0fffULL) | 0x0000000000004000ULL;
    part2 = (part2 & 0x3fffffffffffffffULL) | 0x8000000000000000ULL;

    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(8) << (part1 >> 32) << "-"
        << std::setw(4) << ((part1 >> 16) & 0xffff) << "-"
        << std::setw(4) << (part1 & 0xffff) << "-"
        << std::setw(4) << (part2 >> 48) << "-"
        << std::setw(12) << (part2 & 0xffffffffffffULL);
    return oss.str();
}


/**
 * @brief Compare une valeur dynamique avec une représentation string.
 *
 * Cette fonction permet de comparer un DynamicValue avec une valeur
 * string provenant d'une URL (paramètre HTTP).
 *
 * @param value Valeur dynamique (string, int64, double, bool).
 * @param expected Valeur attendue sous forme de string.
 *
 * @return true si les valeurs correspondent, false sinon.
 *
 * @note Gère les conversions suivantes :
 * - string → comparaison directe
 * - int64 → conversion en string
 * - double → conversion en string
 * - bool → "true" / "false"
 */
[[nodiscard]] bool dynamic_value_matches_string(
    const DynamicValue& value,
    const std::string& expected)
{
    if (std::holds_alternative<std::string>(value)) {
        return std::get<std::string>(value) == expected;
    }

    if (std::holds_alternative<std::int64_t>(value)) {
        return std::to_string(std::get<std::int64_t>(value)) == expected;
    }

    if (std::holds_alternative<double>(value)) {
        std::ostringstream oss;
        oss << std::get<double>(value);
        return oss.str() == expected;
    }

    if (std::holds_alternative<bool>(value)) {
        if (expected == "true") {
            return std::get<bool>(value) == true;
        }
        if (expected == "false") {
            return std::get<bool>(value) == false;
        }
    }

    return false;
}

[[nodiscard]] std::optional<std::string> dynamic_value_to_string(const DynamicValue& value) {
    if (std::holds_alternative<std::string>(value)) {
        return std::get<std::string>(value);
    }

    if (std::holds_alternative<std::int64_t>(value)) {
        return std::to_string(std::get<std::int64_t>(value));
    }

    if (std::holds_alternative<double>(value)) {
        std::ostringstream oss;
        oss << std::get<double>(value);
        return oss.str();
    }

    if (std::holds_alternative<bool>(value)) {
        return std::get<bool>(value) ? "true" : "false";
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<DynamicRecord> find_record_by_field(
    const std::vector<DynamicRecord>& records,
    const std::string& field_name,
    const std::string& expected_value)
{
    for (const auto& record : records) {
        const auto it = record.find(field_name);
        if (it == record.end()) {
            continue;
        }

        if (dynamic_value_matches_string(it->second, expected_value)) {
            return record;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> extract_bearer_token(
    const seastar::http::request& req)
{
    const auto auth_header = req.get_header("Authorization");
    if (auth_header.empty()) {
        return std::nullopt;
    }

    const std::string prefix = "Bearer ";
    const std::string header_value = auth_header;

    if (header_value.rfind(prefix, 0) != 0) {
        return std::nullopt;
    }

    return header_value.substr(prefix.size());
}
[[nodiscard]] std::string get_env_or_default(const char* name, const std::string& default_value) {
    const char* value = std::getenv(name);
    if (!value || std::string(value).empty()) {
        return default_value;
    }
    return std::string(value);
}

/**
 * @brief Convertit un DynamicValue en identifiant string.
 *
 * Cette fonction extrait une représentation string d’un identifiant
 * contenu dans un DynamicValue.
 *
 * @param value Valeur dynamique à convertir.
 *
 * @return std::optional<std::string>
 *         - string contenant l’id si conversion possible
 *         - std::nullopt sinon
 *
 * @note Supporte les types :
 * - std::string
 * - std::int64_t
 */
[[nodiscard]] std::optional<std::string> dynamic_value_to_string_id(const DynamicValue& value) {
    if (std::holds_alternative<std::string>(value)) {
        return std::get<std::string>(value);
    }

    if (std::holds_alternative<std::int64_t>(value)) {
        return std::to_string(std::get<std::int64_t>(value));
    }

    return std::nullopt;
}
// ─────────────────────────────────────────────────────────────
// Handlers HTTP
// ─────────────────────────────────────────────────────────────
/**
 * @brief Ensemble des handlers HTTP du backend.
 *
 * Ces handlers permettent :
 * - d'exposer les opérations CRUD génériques
 * - de gérer les relations entre entités (has_many, belongs_to)
 * - de transformer les requêtes HTTP en appels au moteur CRUD
 *
 * Ils constituent la couche d'adaptation entre le protocole HTTP
 * et le moteur métier (GenericCrudEngine).
 */
class RegisterHandler final : public seastar::httpd::handler_base {
public:
    RegisterHandler(
        std::shared_ptr<GenericCrudEngine> crud_engine,
        std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry> registry,
        std::shared_ptr<sea::application::AuthService> auth_service,
        sea::domain::DatabaseType db_type)
        : crud_engine_(std::move(crud_engine)),
        registry_(std::move(registry)),
        auth_service_(std::move(auth_service)),
        db_type_(db_type) {}

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override {
        const auto* entity = registry_->find_entity("User");
        if (!entity) {
            rep->set_status(seastar::http::reply::status_type::not_found);
            rep->write_body("application/json",
                            json{{"error", "Entite User introuvable."}}.dump());
            co_return std::move(rep);
        }

        try {
            sea::infrastructure::runtime::JsonRecordParser parser;
            auto record = parser.parse(*entity, std::string(req->content));

            const auto all_users = crud_engine_->list("User");

            const auto email_it = record.find("email");
            if (email_it == record.end()) {
                rep->set_status(seastar::http::reply::status_type::bad_request);
                rep->write_body("application/json",
                                json{{"error", "Champ email manquant."}}.dump());
                co_return std::move(rep);
            }

            const auto email = dynamic_value_to_string(email_it->second);
            if (!email.has_value()) {
                rep->set_status(seastar::http::reply::status_type::bad_request);
                rep->write_body("application/json",
                                json{{"error", "Email invalide."}}.dump());
                co_return std::move(rep);
            }

            if (find_record_by_field(all_users, "email", *email).has_value()) {
                rep->set_status(seastar::http::reply::status_type::bad_request);
                rep->write_body("application/json",
                                json{{"error", "Cet email existe deja."}}.dump());
                co_return std::move(rep);
            }

            const auto password_it = record.find("password");
            if (password_it == record.end()) {
                rep->set_status(seastar::http::reply::status_type::bad_request);
                rep->write_body("application/json",
                                json{{"error", "Champ password manquant."}}.dump());
                co_return std::move(rep);
            }

            const auto plain_password = dynamic_value_to_string(password_it->second);
            if (!plain_password.has_value()) {
                rep->set_status(seastar::http::reply::status_type::bad_request);
                rep->write_body("application/json",
                                json{{"error", "Password invalide."}}.dump());
                co_return std::move(rep);
            }

            record["password"] = auth_service_->hash_password(*plain_password);

            if (db_type_ == sea::domain::DatabaseType::Memory) {
                const sea::domain::Field* id_field = nullptr;
                for (const auto& field : entity->fields) {
                    if (field.name == "id") {
                        id_field = &field;
                        break;
                    }
                }

                if (id_field) {
                    if (id_field->type == sea::domain::FieldType::UUID) {
                        std::string new_id;
                        do {
                            new_id = generate_uuid();
                        } while (crud_engine_->get_by_id("User", new_id).has_value());
                        record["id"] = new_id;
                    } else if (id_field->type == sea::domain::FieldType::Int) {
                        record["id"] = generate_int_id("User", crud_engine_);
                    }
                }
            }

            const auto result = crud_engine_->create("User", std::move(record));
            if (!result.success || !result.record.has_value()) {
                rep->set_status(seastar::http::reply::status_type::bad_request);
                rep->write_body("application/json",
                                json{{"error", "Impossible de creer l'utilisateur."}}.dump());
                co_return std::move(rep);
            }

            json user_json = json::parse(record_to_json(*result.record));
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

private:
    std::shared_ptr<GenericCrudEngine> crud_engine_;
    std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry> registry_;
    std::shared_ptr<sea::application::AuthService> auth_service_;
    sea::domain::DatabaseType db_type_;
};



class LoginHandler final : public seastar::httpd::handler_base {
public:
    LoginHandler(
        std::shared_ptr<GenericCrudEngine> crud_engine,
        std::shared_ptr<sea::application::AuthService> auth_service)
        : crud_engine_(std::move(crud_engine)),
        auth_service_(std::move(auth_service)) {}

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override {
        try {
            const auto body = json::parse(std::string(req->content));

            if (!body.contains("email") || !body.contains("password")) {
                rep->set_status(seastar::http::reply::status_type::bad_request);
                rep->write_body("application/json",
                                json{{"error", "email et password sont requis."}}.dump());
                co_return std::move(rep);
            }

            const auto email = body["email"].get<std::string>();
            const auto password = body["password"].get<std::string>();

            const auto users = crud_engine_->list("User");
            const auto user_record = find_record_by_field(users, "email", email);

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

            const auto stored_hash = dynamic_value_to_string(pwd_it->second);
            if (!stored_hash.has_value() ||
                !auth_service_->verify_password(password, *stored_hash)) {
                rep->set_status(seastar::http::reply::status_type::unauthorized);
                rep->write_body("application/json",
                                json{{"error", "Identifiants invalides."}}.dump());
                co_return std::move(rep);
            }

            const auto id_it = user_record->find("id");
            if (id_it == user_record->end()) {
                rep->set_status(seastar::http::reply::status_type::internal_server_error);
                rep->write_body("application/json",
                                json{{"error", "Utilisateur invalide."}}.dump());
                co_return std::move(rep);
            }

            const auto user_id = dynamic_value_to_string_id(id_it->second);
            if (!user_id.has_value()) {
                rep->set_status(seastar::http::reply::status_type::internal_server_error);
                rep->write_body("application/json",
                                json{{"error", "ID utilisateur invalide."}}.dump());
                co_return std::move(rep);
            }

            const auto token = auth_service_->generate_token(*user_id, email);

            json user_json = json::parse(record_to_json(*user_record));
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

private:
    std::shared_ptr<GenericCrudEngine> crud_engine_;
    std::shared_ptr<sea::application::AuthService> auth_service_;
};



class MeHandler final : public seastar::httpd::handler_base {
public:
    MeHandler(
        std::shared_ptr<GenericCrudEngine> crud_engine,
        std::shared_ptr<sea::application::AuthService> auth_service)
        : crud_engine_(std::move(crud_engine)),
        auth_service_(std::move(auth_service)) {}

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override {

        const auto token = extract_bearer_token(*req);
        if (!token.has_value()) {
            rep->set_status(seastar::http::reply::status_type::unauthorized);
            rep->write_body("application/json",
                            json{{"error", "Token manquant."}}.dump());
            co_return std::move(rep);
        }

        const auto claims = auth_service_->verify_token(*token);
        if (!claims.has_value()) {
            rep->set_status(seastar::http::reply::status_type::unauthorized);
            rep->write_body("application/json",
                            json{{"error", "Token invalide."}}.dump());
            co_return std::move(rep);
        }

        const auto user = crud_engine_->get_by_id("User", claims->user_id);
        if (!user.has_value()) {
            rep->set_status(seastar::http::reply::status_type::not_found);
            rep->write_body("application/json",
                            json{{"error", "Utilisateur introuvable."}}.dump());
            co_return std::move(rep);
        }

        json user_json = json::parse(record_to_json(*user));
        user_json.erase("password");

        rep->set_status(seastar::http::reply::status_type::ok);
        rep->write_body("application/json", user_json.dump());
        co_return std::move(rep);
    }

private:
    std::shared_ptr<GenericCrudEngine> crud_engine_;
    std::shared_ptr<sea::application::AuthService> auth_service_;
};


class ProtectedHandler final : public seastar::httpd::handler_base {
public:
    ProtectedHandler(
        std::unique_ptr<seastar::httpd::handler_base> inner,
        std::shared_ptr<sea::application::AuthService> auth_service)
        : inner_(std::move(inner)),
        auth_service_(std::move(auth_service)) {}

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring& path,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override {

        const auto token = extract_bearer_token(*req);
        if (!token.has_value()) {
            rep->set_status(seastar::http::reply::status_type::unauthorized);
            rep->write_body(
                "application/json",
                json{{"error", "Token manquant."}}.dump()
                );
            co_return std::move(rep);
        }

        const auto claims = auth_service_->verify_token(*token);
        if (!claims.has_value()) {
            rep->set_status(seastar::http::reply::status_type::unauthorized);
            rep->write_body(
                "application/json",
                json{{"error", "Token invalide."}}.dump()
                );
            co_return std::move(rep);
        }

        co_return co_await inner_->handle(path, std::move(req), std::move(rep));
    }

private:
    std::unique_ptr<seastar::httpd::handler_base> inner_;
    std::shared_ptr<sea::application::AuthService> auth_service_;
};
[[nodiscard]] seastar::httpd::handler_base* maybe_protect_handler(
    std::unique_ptr<seastar::httpd::handler_base> handler,
    bool requires_auth,
    const std::shared_ptr<sea::application::AuthService>& auth_service)
{
    if (requires_auth) {
        return new ProtectedHandler(std::move(handler), auth_service);
    }
    return handler.release();
}

[[nodiscard]] bool entity_requires_auth(
    const sea::domain::Service& service,
    const std::string& entity_name)
{
    for (const auto& entity : service.schema.entities) {
        if (entity.name == entity_name) {
            return entity.options.enable_auth;
        }
    }
    return false;
}
/**
 * @brief Handler de vérification de l'état du service.
 *
 * Ce handler permet de vérifier que le serveur HTTP est en cours d'exécution.
 * Il ne dépend d'aucune entité ni du moteur CRUD.
 *
 * @route GET /health
 *
 * @response 200 OK
 * {
 *   "status": "RUNNING"
 * }
 *
 * @usage Utilisé pour le monitoring, les tests de disponibilité et SeaUI.
 */
class HealthHandler final : public seastar::httpd::handler_base {
public:
    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request>,
           std::unique_ptr<seastar::http::reply> rep) override {
        rep->set_status(seastar::http::reply::status_type::ok);
        rep->write_body("application/json", "{\"status\":\"RUNNING\"}");
        co_return std::move(rep);
    }
};

/**
 * @brief Handler d'exposition du document OpenAPI.
 *
 * Ce handler retourne le document OpenAPI généré automatiquement
 * pour le service courant.
 *
 * @route GET /openapi.json
 *
 * @response 200 OK
 * Document OpenAPI au format JSON.
 */
class OpenApiHandler final : public seastar::httpd::handler_base {
public:
    explicit OpenApiHandler(std::string body)
        : body_(std::move(body)) {}

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request>,
           std::unique_ptr<seastar::http::reply> rep) override {
        rep->set_status(seastar::http::reply::status_type::ok);
        rep->write_body("application/json", body_);
        co_return std::move(rep);
    }

private:
    std::string body_;
};

/**
 * @brief Handler d'exposition de Swagger UI.
 *
 * Ce handler retourne une page HTML simple qui charge Swagger UI
 * et consomme automatiquement le document OpenAPI expose par /openapi.json.
 *
 * @route GET /docs
 *
 * @response 200 OK
 * Page HTML Swagger UI.
 */
class SwaggerUiHandler final : public seastar::httpd::handler_base {
public:
    SwaggerUiHandler() = default;

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request>,
           std::unique_ptr<seastar::http::reply> rep) override {
        static const std::string html = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <title>Swagger UI</title>
  <link rel="stylesheet" href="https://unpkg.com/swagger-ui-dist/swagger-ui.css" />
  <style>
    html, body {
      margin: 0;
      padding: 0;
      height: 100%;
      background: #fafafa;
    }
    #swagger-ui {
      height: 100%;
    }
  </style>
</head>
<body>
  <div id="swagger-ui"></div>

  <script src="https://unpkg.com/swagger-ui-dist/swagger-ui-bundle.js"></script>
  <script src="https://unpkg.com/swagger-ui-dist/swagger-ui-standalone-preset.js"></script>
  <script>
    window.onload = () => {
      window.ui = SwaggerUIBundle({
        url: '/openapi.json',
        dom_id: '#swagger-ui',
        deepLinking: true,
        presets: [
          SwaggerUIBundle.presets.apis,
          SwaggerUIStandalonePreset
        ],
        layout: "StandaloneLayout"
      });
    };
  </script>
</body>
</html>
)HTML";

        rep->set_status(seastar::http::reply::status_type::ok);
        rep->write_body("text/html; charset=utf-8", html);
        co_return std::move(rep);
    }
};

/**
 * @brief Handler de récupération de tous les enregistrements d'une entité.
 *
 * Ce handler appelle le moteur CRUD pour récupérer la liste complète des
 * enregistrements associés à une entité donnée.
 *
 * @route GET /{entity}
 *
 * @param entity_name Nom de l'entité à interroger (ex: "Product").
 *
 * @response 200 OK
 * [
 *   { "id": 1, "title": "Laptop" },
 *   { "id": 2, "title": "Mouse" }
 * ]
 *
 * @note Aucun paramètre n'est requis.
 */
class ListHandler final : public seastar::httpd::handler_base {
public:
    ListHandler(std::shared_ptr<GenericCrudEngine> crud_engine, std::string entity_name)
        : crud_engine_(std::move(crud_engine)),
        entity_name_(std::move(entity_name)) {
    }

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request>,
           std::unique_ptr<seastar::http::reply> rep) override {
        const auto records = crud_engine_->list(entity_name_);
        rep->set_status(seastar::http::reply::status_type::ok);
        rep->write_body("application/json", records_to_json(records));
        co_return std::move(rep);
    }

private:
    std::shared_ptr<GenericCrudEngine> crud_engine_;
    std::string entity_name_;
};


/**
 * @brief Handler de récupération des enfants via un champ unique du parent.
 *
 * Ce handler permet de retrouver un parent à partir d'un champ unique
 * (ex: code, email, name) puis de récupérer ses enfants associés.
 *
 * @route GET /{child_entity}/filter/with_{parent}_{field}/{value}
 *
 * @param value Valeur du champ unique du parent.
 * @param search_field Nom du champ à rechercher (ex: "code").
 *
 * @example
 * GET /employees/filter/with_department_code/HR
 *
 * @response 200 OK
 * [
 *   { "id": 1, "name": "Alice", "department_id": "1" }
 * ]
 *
 * @note Utile quand l'id du parent n'est pas connu côté client.
 */
class GetWithChildrenHandler final : public seastar::httpd::handler_base {
public:
    GetWithChildrenHandler(
        std::shared_ptr<GenericCrudEngine> crud_engine,
        std::string parent_entity,
        std::string child_entity,
        std::string fk_column,
        std::string children_key)
        : crud_engine_(std::move(crud_engine)),
        parent_entity_(std::move(parent_entity)),
        child_entity_(std::move(child_entity)),
        fk_column_(std::move(fk_column)),
        children_key_(std::move(children_key)) {}

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override {

        const auto parent_id = std::string(req->get_path_param("id"));
        if (parent_id.empty()) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body(
                "application/json",
                json{{"error", "Parametre 'id' manquant."}}.dump()
                );
            co_return std::move(rep);
        }

        const auto parent = crud_engine_->get_by_id(parent_entity_, parent_id);
        if (!parent.has_value()) {
            rep->set_status(seastar::http::reply::status_type::not_found);
            rep->write_body(
                "application/json",
                json{{"error", parent_entity_ + " introuvable."}}.dump()
                );
            co_return std::move(rep);
        }

        json result = json::parse(record_to_json(*parent));

        const auto all_children = crud_engine_->list(child_entity_);
        json children = json::array();

        for (const auto& record : all_children) {
            const auto it = record.find(fk_column_);
            if (it == record.end()) {
                continue;
            }

            if (dynamic_value_matches_string(it->second, parent_id)) {
                children.push_back(json::parse(record_to_json(record)));
            }
        }

        result[children_key_] = children;

        rep->set_status(seastar::http::reply::status_type::ok);
        rep->write_body("application/json", result.dump());
        co_return std::move(rep);
    }

private:
    std::shared_ptr<GenericCrudEngine> crud_engine_;
    std::string parent_entity_;
    std::string child_entity_;
    std::string fk_column_;
    std::string children_key_;
};


/**
 * @brief Handler de récupération des enregistrements liés par clé étrangère.
 *
 * Ce handler implémente une relation de type "has_many".
 * Il retourne tous les enregistrements enfants associés à un parent donné.
 *
 * @route GET /{child_entity}/filter/with_{parent}/{id}
 *
 * @param id Identifiant du parent.
 * @param fk_column Nom de la clé étrangère (ex: "department_id").
 *
 * @example
 * GET /employees/filter/with_department/1
 *
 * @response 200 OK
 * [
 *   { "id": 1, "name": "Alice", "department_id": "1" },
 *   { "id": 2, "name": "Bob", "department_id": "1" }
 * ]
 *
 * @note Effectue un filtrage en mémoire basé sur la clé étrangère.
 */
class ListByFkHandler final : public seastar::httpd::handler_base {
public:
    ListByFkHandler(
        std::shared_ptr<GenericCrudEngine> crud_engine,
        std::string child_entity,
        std::string fk_column)
        : crud_engine_(std::move(crud_engine)),
        child_entity_(std::move(child_entity)),
        fk_column_(std::move(fk_column)) {}

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override {

        const auto parent_id = std::string(req->get_path_param("id"));
        if (parent_id.empty()) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body(
                "application/json",
                json{{"error", "Parametre 'id' manquant."}}.dump()
                );
            co_return std::move(rep);
        }

        const auto all_records = crud_engine_->list(child_entity_);
        std::vector<DynamicRecord> filtered;

        for (const auto& record : all_records) {
            const auto it = record.find(fk_column_);
            if (it == record.end()) {
                continue;
            }

            if (dynamic_value_matches_string(it->second, parent_id)) {
                filtered.push_back(record);
            }
        }

        rep->set_status(seastar::http::reply::status_type::ok);
        rep->write_body("application/json", records_to_json(filtered));
        co_return std::move(rep);
    }

private:
    std::shared_ptr<GenericCrudEngine> crud_engine_;
    std::string child_entity_;
    std::string fk_column_;
};


/**
 * @brief Handler de récupération des enfants via un champ unique du parent.
 *
 * Ce handler permet de retrouver un parent à partir d'un champ unique
 * (ex: code, email, name) puis de récupérer ses enfants associés.
 *
 * @route GET /{child_entity}/filter/with_{parent}_{field}/{value}
 *
 * @param value Valeur du champ unique du parent.
 * @param search_field Nom du champ à rechercher (ex: "code").
 *
 * @example
 * GET /employees/filter/with_department_code/HR
 *
 * @response 200 OK
 * [
 *   { "id": 1, "name": "Alice", "department_id": "1" }
 * ]
 *
 * @note Utile quand l'id du parent n'est pas connu côté client.
 */
class ListByFkFieldHandler final : public seastar::httpd::handler_base {
public:
    ListByFkFieldHandler(
        std::shared_ptr<GenericCrudEngine> crud_engine,
        std::string child_entity,
        std::string parent_entity,
        std::string fk_column,
        std::string search_field)
        : crud_engine_(std::move(crud_engine)),
        child_entity_(std::move(child_entity)),
        parent_entity_(std::move(parent_entity)),
        fk_column_(std::move(fk_column)),
        search_field_(std::move(search_field)) {}

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override {

        const auto search_value = std::string(req->get_path_param(search_field_));

        std::cerr << "[LISTBYFK] search_field=" << search_field_
                  << " search_value=" << search_value
                  << " parent_entity=" << parent_entity_ << std::endl;

        if (search_value.empty()) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body(
                "application/json",
                json{{"error", "Parametre '" + search_field_ + "' manquant."}}.dump()
                );
            co_return std::move(rep);
        }

        const auto all_parents = crud_engine_->list(parent_entity_);
        std::string parent_id;

        for (const auto& record : all_parents) {
            const auto it = record.find(search_field_);
            if (it == record.end()) {
                continue;
            }

            if (!dynamic_value_matches_string(it->second, search_value)) {
                continue;
            }

            const auto id_it = record.find("id");
            if (id_it == record.end()) {
                continue;
            }

            const auto id_value = dynamic_value_to_string_id(id_it->second);
            if (id_value.has_value()) {
                parent_id = *id_value;
            }
            break;
        }

        if (parent_id.empty()) {
            rep->set_status(seastar::http::reply::status_type::not_found);
            rep->write_body(
                "application/json",
                json{{"error", parent_entity_ + " introuvable avec " +
                                   search_field_ + "=" + search_value}}.dump()
                );
            co_return std::move(rep);
        }

        const auto all_children = crud_engine_->list(child_entity_);
        std::vector<DynamicRecord> filtered;

        for (const auto& record : all_children) {
            const auto it = record.find(fk_column_);
            if (it == record.end()) {
                continue;
            }

            if (dynamic_value_matches_string(it->second, parent_id)) {
                filtered.push_back(record);
            }
        }

        rep->set_status(seastar::http::reply::status_type::ok);
        rep->write_body("application/json", records_to_json(filtered));
        co_return std::move(rep);
    }

private:
    std::shared_ptr<GenericCrudEngine> crud_engine_;
    std::string child_entity_;
    std::string parent_entity_;
    std::string fk_column_;
    std::string search_field_;
};

/**
 * @brief Handler de récupération d'un enregistrement par identifiant.
 *
 * Ce handler récupère un enregistrement unique à partir de son identifiant
 * en utilisant le moteur CRUD.
 *
 * @route GET /{entity}/{id}
 *
 * @param id Identifiant de l'enregistrement (chemin URL).
 * @param entity_name Nom de l'entité.
 *
 * @response 200 OK
 * { "id": 1, "title": "Laptop" }
 *
 * @response 404 Not Found
 * { "error": "Enregistrement introuvable." }
 *
 * @response 400 Bad Request
 * { "error": "Parametre 'id' manquant." }
 */
class GetByIdHandler final : public seastar::httpd::handler_base {
public:
    GetByIdHandler(std::shared_ptr<GenericCrudEngine> crud_engine, std::string entity_name)
        : crud_engine_(std::move(crud_engine)),
        entity_name_(std::move(entity_name)) {
    }

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override {
        const auto id = req->get_path_param("id");
        std::cerr << "[DEBUG] url = " << req->get_url() << std::endl;
        std::cerr << "[DEBUG] query id = [" << req->get_query_param("id") << "]" << std::endl;
        std::cerr << "[DEBUG] path id  = [" << req->get_path_param("id") << "]" << std::endl;
        if (id.empty()) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("text/plain", "Parametre 'id' manquant.");
            co_return std::move(rep);
        }

        const auto record = crud_engine_->get_by_id(entity_name_, std::string(id));

        if (!record.has_value()) {
            rep->set_status(seastar::http::reply::status_type::not_found);
            rep->write_body("text/plain", "Enregistrement introuvable.");
            co_return std::move(rep);
        }

        rep->set_status(seastar::http::reply::status_type::ok);
        rep->write_body("application/json", record_to_json(*record));
        co_return std::move(rep);
    }

private:
    std::shared_ptr<GenericCrudEngine> crud_engine_;
    std::string entity_name_;
};



/**
 * @brief Handler de récupération d'un enregistrement enfant via une clé étrangère.
 *
 * Ce handler implémente une relation de type "has_one".
 * Il retourne un seul enregistrement enfant lié à un parent donné,
 * en se basant sur une clé étrangère.
 *
 * @route GET /{parent}/{relation}/{id}
 *
 * @param id Identifiant du parent (chemin URL).
 * @param fk_column Nom de la clé étrangère dans l'entité enfant.
 *
 * @example
 * GET /users/profile/1
 *
 * @response 200 OK
 * { "id": 10, "bio": "Developer", "user_id": "1" }
 *
 * @response 404 Not Found
 * { "error": "Enregistrement lie introuvable." }
 *
 * @response 400 Bad Request
 * { "error": "Parametre 'id' manquant." }
 *
 * @note Supporte les types de clé étrangère string et int64.
 */
class GetOneByFkHandler final : public seastar::httpd::handler_base {
public:
    GetOneByFkHandler(
        std::shared_ptr<GenericCrudEngine> crud_engine,
        std::string child_entity,
        std::string fk_column)
        : crud_engine_(std::move(crud_engine)),
        child_entity_(std::move(child_entity)),
        fk_column_(std::move(fk_column)) {}

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override {

        const auto parent_id = std::string(req->get_path_param("id"));
        if (parent_id.empty()) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body(
                "application/json",
                json{{"error", "Parametre 'id' manquant."}}.dump()
                );
            co_return std::move(rep);
        }

        const auto all_records = crud_engine_->list(child_entity_);

        for (const auto& record : all_records) {
            const auto it = record.find(fk_column_);
            if (it == record.end()) {
                continue;
            }

            if (dynamic_value_matches_string(it->second, parent_id)) {
                rep->set_status(seastar::http::reply::status_type::ok);
                rep->write_body("application/json", record_to_json(record));
                co_return std::move(rep);
            }
        }

        rep->set_status(seastar::http::reply::status_type::not_found);
        rep->write_body(
            "application/json",
            json{{"error", "Enregistrement lie introuvable."}}.dump()
            );
        co_return std::move(rep);
    }

private:
    std::shared_ptr<GenericCrudEngine> crud_engine_;
    std::string child_entity_;
    std::string fk_column_;
};



/**
 * @brief Handler de récupération d'enregistrements liés via une relation many-to-many.
 *
 * Ce handler permet de retrouver les entités cibles associées à une entité source
 * en passant par une entité pivot.
 *
 * @route GET /{target_entity}/filter/with_{source_entity}/{id}
 *
 * @param id Identifiant de l'entité source.
 * @param pivot_table Nom de l'entité/table pivot.
 * @param source_fk_column Nom de la clé étrangère vers l'entité source dans la pivot.
 * @param target_fk_column Nom de la clé étrangère vers l'entité cible dans la pivot.
 *
 * @example
 * GET /courses/filter/with_student/1
 *
 * @response 200 OK
 * [
 *   { "id": 10, "title": "Math" },
 *   { "id": 11, "title": "Physics" }
 * ]
 *
 * @response 400 Bad Request
 * { "error": "Parametre 'id' manquant." }
 *
 * @note Cette route repose sur une entité pivot explicite.
 */
class ListManyToManyHandler final : public seastar::httpd::handler_base {
public:
    ListManyToManyHandler(
        std::shared_ptr<GenericCrudEngine> crud_engine,
        std::string pivot_table,
        std::string target_entity,
        std::string source_fk_column,
        std::string target_fk_column)
        : crud_engine_(std::move(crud_engine)),
        pivot_table_(std::move(pivot_table)),
        target_entity_(std::move(target_entity)),
        source_fk_column_(std::move(source_fk_column)),
        target_fk_column_(std::move(target_fk_column)) {}

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override {

        const auto source_id = std::string(req->get_path_param("id"));
        if (source_id.empty()) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body(
                "application/json",
                json{{"error", "Parametre 'id' manquant."}}.dump()
                );
            co_return std::move(rep);
        }

        const auto pivot_records = crud_engine_->list(pivot_table_);
        std::vector<std::string> target_ids;
        target_ids.reserve(pivot_records.size());

        for (const auto& record : pivot_records) {
            const auto src_it = record.find(source_fk_column_);
            if (src_it == record.end()) {
                continue;
            }

            if (!dynamic_value_matches_string(src_it->second, source_id)) {
                continue;
            }

            const auto tgt_it = record.find(target_fk_column_);
            if (tgt_it == record.end()) {
                continue;
            }

            const auto target_id = dynamic_value_to_string_id(tgt_it->second);
            if (target_id.has_value()) {
                target_ids.push_back(*target_id);
            }
        }

        std::vector<DynamicRecord> results;
        results.reserve(target_ids.size());

        for (const auto& target_id : target_ids) {
            const auto target_record = crud_engine_->get_by_id(target_entity_, target_id);
            if (target_record.has_value()) {
                results.push_back(*target_record);
            }
        }

        rep->set_status(seastar::http::reply::status_type::ok);
        rep->write_body("application/json", records_to_json(results));
        co_return std::move(rep);
    }

private:
    std::shared_ptr<GenericCrudEngine> crud_engine_;
    std::string pivot_table_;
    std::string target_entity_;
    std::string source_fk_column_;
    std::string target_fk_column_;
};




/**
 * @brief Handler de création d'un nouvel enregistrement.
 *
 * Ce handler :
 * - Parse le body JSON de la requête
 * - Valide les données selon le schéma (via JsonRecordParser)
 * - Génère automatiquement un identifiant pour les bases mémoire
 * - Insère l'enregistrement via le moteur CRUD
 *
 * @route POST /{entity}
 *
 * @param body JSON contenant les champs de l'entité.
 *
 * @example
 * {
 *   "title": "Laptop",
 *   "price": 999.99
 * }
 *
 * @response 200 OK
 * {
 *   "id": 1,
 *   "title": "Laptop",
 *   "price": 999.99
 * }
 *
 * @note L'id est généré automatiquement si la base est de type Memory.
 */
class CreateHandler final : public seastar::httpd::handler_base {
public:
    CreateHandler(
        std::shared_ptr<GenericCrudEngine> crud_engine,
        std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry> registry,
        std::string entity_name, sea::domain::DatabaseType db_type)
        : crud_engine_(std::move(crud_engine)),
        registry_(std::move(registry)),
        entity_name_(std::move(entity_name)),
        db_type_(std::move(db_type))  {
    }

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override {
        std::cerr << "[HANDLER] CreateHandler for entity = "
                  << entity_name_ << std::endl;

        const auto* entity = registry_->find_entity(entity_name_);
        if (!entity) {
            rep->set_status(seastar::http::reply::status_type::not_found);
            rep->write_body("text/plain", "Entite inconnue.");
            co_return std::move(rep);
        }

        try {
            // Version MVP : on garde req->content.
            // Oui, c'est deprecated, mais on corrige d'abord le POST.
            // On passera ensuite a content_stream.
            sea::infrastructure::runtime::JsonRecordParser parser;
            auto record = parser.parse(*entity, std::string(req->content));

            // ── Génération automatique de l'id ──────────────────────────
            if (db_type_ == sea::domain::DatabaseType::Memory) {
                const sea::domain::Field* id_field = nullptr;
                for (const auto& field : entity->fields) {
                    if (field.name == "id") { id_field = &field; break; }
                }

                if (id_field) {
                    if (id_field->type == sea::domain::FieldType::UUID) {
                        std::string new_id;
                        do {
                            new_id = generate_uuid();
                        } while (crud_engine_->get_by_id(entity_name_, new_id).has_value());
                        record["id"] = new_id;
                    } else if (id_field->type == sea::domain::FieldType::Int) {
                        record["id"] = generate_int_id(entity_name_, crud_engine_);
                    }
                }
            }
            const auto result = crud_engine_->create(entity_name_, std::move(record));

            if (!result.success) {
                std::ostringstream oss;
                oss << "{ \"errors\": [";
                for (std::size_t i = 0; i < result.errors.size(); ++i) {
                    if (i != 0) {
                        oss << ",";
                    }
                    oss << "\"" << json_escape(result.errors[i]) << "\"";
                }
                oss << "] }";

                rep->set_status(seastar::http::reply::status_type::bad_request);
                rep->write_body("application/json", oss.str());
                co_return std::move(rep);
            }

            rep->set_status(seastar::http::reply::status_type::created);
            auto json_result = record_to_json(*result.record);
            std::cerr << "[UPDATE] json result = " << json_result << std::endl;
            rep->write_body("application/json", record_to_json(*result.record));
            co_return std::move(rep);

        } catch (const std::exception& e) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("text/plain", std::string("Erreur JSON: ") + e.what());
            co_return std::move(rep);
        }
    }

private:
    std::shared_ptr<GenericCrudEngine> crud_engine_;
    std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry> registry_;
    std::string entity_name_;
    sea::domain::DatabaseType db_type_;

};

/**
 * @brief Handler de mise à jour d'un enregistrement existant.
 *
 * Ce handler :
 * - Récupère l'identifiant depuis l'URL
 * - Parse le body JSON contenant les nouvelles valeurs
 * - Met à jour l'enregistrement via le moteur CRUD
 *
 * @route PUT /{entity}/{id}
 *
 * @param id Identifiant de l'enregistrement à modifier.
 * @param body JSON avec les champs à mettre à jour.
 *
 * @response 200 OK
 * {
 *   "id": 1,
 *   "title": "Laptop Pro",
 *   "price": 1299.99
 * }
 *
 * @response 404 Not Found
 * { "error": "Enregistrement introuvable." }
 *
 * @response 400 Bad Request
 * { "error": "Parametre 'id' manquant." }
 */
class UpdateHandler final : public seastar::httpd::handler_base {
public:
    UpdateHandler(
        std::shared_ptr<GenericCrudEngine> crud_engine,
        std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry> registry,
        std::string entity_name)
        : crud_engine_(std::move(crud_engine)),
        registry_(std::move(registry)),
        entity_name_(std::move(entity_name)) {}

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override {

        const auto id = req->get_path_param("id");
        if (id.empty()) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("text/plain", "Parametre 'id' manquant.");
            co_return std::move(rep);
        }

        const auto* entity = registry_->find_entity(entity_name_);
        if (!entity) {
            rep->set_status(seastar::http::reply::status_type::not_found);
            rep->write_body("text/plain", "Entite inconnue.");
            co_return std::move(rep);
        }

        try {
            sea::infrastructure::runtime::JsonRecordParser parser;
            auto record = parser.parse(*entity, std::string(req->content));
            std::cerr << "[UPDATE] body recu = " << std::string(req->content) << std::endl;
            const auto result = crud_engine_->update(entity_name_, std::string(id), std::move(record));

            if (!result.success) {
                std::ostringstream oss;
                oss << "{ \"errors\": [";
                for (std::size_t i = 0; i < result.errors.size(); ++i) {
                    if (i != 0) oss << ",";
                    oss << "\"" << json_escape(result.errors[i]) << "\"";
                }
                oss << "] }";
                rep->set_status(seastar::http::reply::status_type::bad_request);
                rep->write_body("application/json", oss.str());
                co_return std::move(rep);
            }

            rep->set_status(seastar::http::reply::status_type::ok);
            rep->write_body("application/json", record_to_json(*result.record));
            co_return std::move(rep);

        } catch (const std::exception& e) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("application/json", json{{"bad_request", std::string("Erreur JSON: ") + e.what()}}.dump());
            co_return std::move(rep);
        }
    }

private:
    std::shared_ptr<GenericCrudEngine> crud_engine_;
    std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry> registry_;
    std::string entity_name_;
};


/**
 * @brief Handler de suppression d'un enregistrement.
 *
 * Ce handler supprime un enregistrement en fonction de son identifiant.
 *
 * @route DELETE /{entity}/{id}
 *
 * @param id Identifiant de l'enregistrement à supprimer.
 *
 * @response 200 OK
 * { "message": "Deleted" }
 *
 * @response 404 Not Found
 * { "error": "Enregistrement introuvable." }
 *
 * @response 400 Bad Request
 * { "error": "Parametre 'id' manquant." }
 */
class DeleteHandler final : public seastar::httpd::handler_base {
public:
    DeleteHandler(std::shared_ptr<GenericCrudEngine> crud_engine, std::string entity_name)
        : crud_engine_(std::move(crud_engine)),
        entity_name_(std::move(entity_name)) {}

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override {

        const auto id = req->get_path_param("id");
        std::cerr << "[HANDLER] DeleteHandler for entity = "
                  << entity_name_ << " id = " << id << std::endl;

        if (id.empty()) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("application/json", json{{"error", "Parametre 'id' manquant."}}.dump());
            co_return std::move(rep);
        }
        try {
            const bool deleted = crud_engine_->remove(entity_name_, std::string(id));
            if (!deleted) {
                rep->set_status(seastar::http::reply::status_type::not_found);
                rep->write_body("application/json", json{{"error", "Enregistrement introuvable."}}.dump());
                co_return std::move(rep);
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

private:
    std::shared_ptr<GenericCrudEngine> crud_engine_;
    std::string entity_name_;
};
} // namespace

int main(int argc, char** argv) {
    seastar::app_template app;

    app.add_options()
        ("config", bpo::value<std::string>()->required(), "Chemin du fichier YAML")
        ("service_name", bpo::value<std::string>()->required(), "Nom du service a lancer");

    return app.run(argc, argv, [&app] {
        const auto& cfg = app.configuration();

        const std::string config_path = cfg["config"].as<std::string>();
        const std::string service_name = cfg["service_name"].as<std::string>();

        std::cerr << "[1] config = " << config_path << std::endl;
        std::cerr << "[2] service = " << service_name << std::endl;

        sea::application::ImportYamlSchemaUseCase importer;
        const auto project = importer.execute(config_path);

        if (project.empty()) {
            throw std::runtime_error("Aucun service defini dans le projet.");
        }

        const sea::domain::Service* selected_service = nullptr;

        for (const auto& service : project.services) {
            if (service.name == service_name) {
                selected_service = &service;
                break;
            }
        }

        if (!selected_service) {
            throw std::runtime_error("Service introuvable: " + service_name);
        }

        const auto& service = *selected_service;

        sea::application::ValidateSchemaUseCase validate_usecase;
        const auto validation = validate_usecase.execute(service);

        if (!validation.valid) {
            std::ostringstream oss;
            oss << "Schema invalide : ";
            for (const auto& err : validation.errors) {
                oss << err << " ; ";
            }
            throw std::runtime_error(oss.str());
        }

        auto registry =
            std::make_shared<sea::infrastructure::runtime::SchemaRuntimeRegistry>();

        auto repository_factory =
            std::make_shared<sea::infrastructure::persistence::RepositoryFactory>();

        sea::application::StartServiceUseCase start_usecase(*registry, *repository_factory);
        auto repository =
            std::shared_ptr<sea::infrastructure::persistence::IGenericRepository>(
                std::move(start_usecase.execute(service))
                );

        auto validator =
            std::make_shared<sea::infrastructure::runtime::GenericValidator>();

        auto crud_engine =
            std::make_shared<sea::infrastructure::runtime::GenericCrudEngine>(
                registry,
                validator,
                repository
                );

        sea::application::RouteGenerator route_generator;
        const auto route_definitions = route_generator.generate(service.schema);
        sea::application::OpenApiGenerator openapi_generator;
        const auto openapi_doc = openapi_generator.generate(service, route_definitions);
        const auto openapi_json = openapi_doc.dump(2);
        std::cerr << "========== ROUTES POUR " << service.name << " ==========" << std::endl;

        for (const auto& route : route_definitions) {
            const char* method = "UNKNOWN";

            switch (route.method) {
            case sea::application::HttpMethod::Get:    method = "GET"; break;
            case sea::application::HttpMethod::Post:   method = "POST"; break;
            case sea::application::HttpMethod::Put:    method = "PUT"; break;
            case sea::application::HttpMethod::Delete: method = "DELETE"; break;
            }

            std::cerr << method
                      << "  " << route.path
                      << "  [" << route.entity_name
                      << " / " << route.operation_name << "]"
                      << std::endl;
        }

        std::cerr << "==========================================" << std::endl;
        auto server = std::make_shared<seastar::httpd::http_server_control>();
        const auto jwt_secret = get_env_or_default("SEA_JWT_SECRET", "dev_only_secret_change_me");

        auto auth_service =
            std::make_shared<sea::application::AuthService>(jwt_secret);
        return server->start()
            .then([server, crud_engine, registry, route_definitions, service, openapi_json, auth_service] {
                std::cerr << "[9] apres start(), avant set_routes()" << std::endl;

                return server->set_routes(
                    [crud_engine, registry, route_definitions, service, openapi_json, auth_service](seastar::httpd::routes& r) {
                        std::cerr << "[10] dans set_routes()" << std::endl;

                        // Route de santé
                        r.add(
                            seastar::httpd::operation_type::GET,
                            seastar::httpd::url("/health"),
                            new HealthHandler()
                            );

                        // Route OpenAPI
                        r.add(
                            seastar::httpd::operation_type::GET,
                            seastar::httpd::url("/openapi.json"),
                            new OpenApiHandler(openapi_json)
                            );

                        // Route Swagger UI
                        r.add(
                            seastar::httpd::operation_type::GET,
                            seastar::httpd::url("/docs"),
                            new SwaggerUiHandler()
                            );
                        // Route d authentification
                        r.add(
                            seastar::httpd::operation_type::POST,
                            seastar::httpd::url("/auth/register"),
                            new RegisterHandler(crud_engine, registry, auth_service, service.database_config.type)
                            );

                        r.add(
                            seastar::httpd::operation_type::POST,
                            seastar::httpd::url("/auth/login"),
                            new LoginHandler(crud_engine, auth_service)
                            );

                        r.add(
                            seastar::httpd::operation_type::GET,
                            seastar::httpd::url("/auth/me"),
                            new MeHandler(crud_engine, auth_service)
                            );
                        // =========================================================
                        // PASS 1 : routes sans id
                        // =========================================================
                        for (const auto& route : route_definitions) {
                            if (route.method == sea::application::HttpMethod::Get &&
                                route.operation_name == "list") {
                                std::cerr << "[ROUTE] GET " << route.path
                                          << " -> ListHandler" << std::endl;
                                const bool requires_auth = entity_requires_auth(service, route.entity_name);

                                r.add(
                                    seastar::httpd::operation_type::GET,
                                    seastar::httpd::url(route.path),
                                    maybe_protect_handler(
                                        std::make_unique<ListHandler>(crud_engine, route.entity_name),
                                        requires_auth,
                                        auth_service
                                        )
                                    );
                            }

                            if (route.method == sea::application::HttpMethod::Post &&
                                route.operation_name == "create") {
                                std::cerr << "[ROUTE] POST " << route.path
                                          << " -> CreateHandler" << std::endl;
                                const bool requires_auth = entity_requires_auth(service, route.entity_name);

                                r.add(
                                    seastar::httpd::operation_type::POST,
                                    seastar::httpd::url(route.path),
                                    maybe_protect_handler(
                                        std::make_unique<CreateHandler>(
                                            crud_engine,
                                            registry,
                                            route.entity_name,
                                            service.database_config.type
                                            ),
                                        requires_auth,
                                        auth_service
                                        )
                                    );
                            }
                        }
                        // =========================================================
                        // PASS 2 : routes HasMany
                        // =========================================================
                        std::cerr << "[PASS2] nb entities=" << service.schema.entities.size() << std::endl;
                        for (const auto& entity_def : service.schema.entities) {
                            std::cerr << "[PASS2] entity=" << entity_def.name
                                      << " nb relations=" << entity_def.relations.size() << std::endl;
                            for (const auto& relation : entity_def.relations) {
                                std::cerr << "[PASS2] relation=" << relation.name
                                          << " kind=" << static_cast<int>(relation.kind) << std::endl;
                            }
                        }
                        for (const auto& entity_def : service.schema.entities) {
                            for (const auto& relation : entity_def.relations) {
                                if (relation.kind != sea::domain::RelationKind::HasMany) continue;

                                std::string child_path = "/" + relation.target_entity;
                                child_path[1] = static_cast<char>(std::tolower(child_path[1]));
                                child_path += "s";

                                std::string parent_name = entity_def.name;
                                parent_name[0] = static_cast<char>(std::tolower(parent_name[0]));

                                // Route par id
                                std::string route_path = child_path + "/filter/with_" + parent_name;
                                const bool requires_auth = entity_def.options.enable_auth;

                                r.add(
                                    seastar::httpd::operation_type::GET,
                                    seastar::httpd::url(route_path).remainder("id"),
                                    maybe_protect_handler(
                                        std::make_unique<ListByFkHandler>(crud_engine, relation.target_entity, relation.fk_column),
                                        requires_auth, auth_service)
                                    );

                                // Trouver l'entité parent pour accéder à ses champs
                                const sea::domain::Entity* parent_entity = nullptr;
                                for (const auto& e : service.schema.entities) {
                                    if (e.name == entity_def.name) {
                                        parent_entity = &e;
                                        break;
                                    }
                                }

                                if (!parent_entity) continue;

                                // Route par chaque champ unique (sauf id)
                                for (const auto& field : parent_entity->fields) {
                                    if (!field.unique || field.name == "id") continue;

                                    std::string route_by_field = child_path + "/filter/with_" + parent_name + "_" + field.name;

                                    std::cerr << "[ROUTE] GET " << route_by_field << "/<" << field.name << ">"
                                              << " -> ListByFkFieldHandler" << std::endl;
                                    const bool requires_auth = entity_def.options.enable_auth;

                                    r.add(
                                        seastar::httpd::operation_type::GET,
                                        seastar::httpd::url(route_by_field).remainder(field.name),
                                        maybe_protect_handler(
                                            std::make_unique<ListByFkFieldHandler>(
                                            crud_engine,
                                            relation.target_entity,
                                            entity_def.name,
                                            relation.fk_column,
                                            field.name
                                            ),
                                            requires_auth,
                                            auth_service)
                                        );
                                }
                                // Route department_with_employees
                                std::string parent_path = "/" + entity_def.name;
                                parent_path[1] = static_cast<char>(std::tolower(parent_path[1]));
                                parent_path += "s";

                                std::string relation_name = relation.name; // ex: "employees"
                                std::string route_with_children = parent_path + "_with_" + relation_name;

                                std::cerr << "[ROUTE] GET " << route_with_children << "/<id>"
                                          << " -> GetWithChildrenHandler" << std::endl;

                                r.add(
                                    seastar::httpd::operation_type::GET,
                                    seastar::httpd::url(route_with_children).remainder("id"),
                                    maybe_protect_handler(
                                        std::make_unique<GetWithChildrenHandler>(
                                        crud_engine,
                                        entity_def.name,        // parent = Department
                                        relation.target_entity, // child = Employee
                                        relation.fk_column,     // fk = department_id
                                        relation.name           // key = employees
                                        ),
                                        requires_auth,
                                        auth_service)
                                    );
                            }
                        }
                        // =========================================================
                        // PASS 2B : routes HasOne
                        // Format retenu : /parents/<relation>/<id>
                        // Exemple : /users/profile/1
                        // =========================================================
                        for (const auto& entity_def : service.schema.entities) {
                            for (const auto& relation : entity_def.relations) {
                                if (relation.kind != sea::domain::RelationKind::HasOne) {
                                    continue;
                                }

                                std::string parent_path = "/" + entity_def.name;
                                parent_path[1] = static_cast<char>(std::tolower(parent_path[1]));
                                parent_path += "s";

                                std::string relation_path = relation.name; // ex: "profile"

                                // Route robuste avec un seul remainder("id")
                                std::string full_path = parent_path + "/" + relation_path;

                                std::cerr << "[ROUTE] GET " << full_path << "/<id>"
                                          << " -> GetOneByFkHandler" << std::endl;
                                const bool requires_auth = entity_def.options.enable_auth;

                                r.add(
                                    seastar::httpd::operation_type::GET,
                                    seastar::httpd::url(full_path).remainder("id"),
                                    maybe_protect_handler(
                                        std::make_unique<GetOneByFkHandler>(
                                        crud_engine,
                                        relation.target_entity,
                                        relation.fk_column
                                        ),
                                        requires_auth,
                                        auth_service)
                                    );
                            }
                        }
                        // =========================================================
                        // PASS 2C : routes ManyToMany
                        // Exemple : /courses/filter/with_student/<id>
                        // =========================================================
                        for (const auto& entity_def : service.schema.entities) {
                            for (const auto& relation : entity_def.relations) {
                                if (relation.kind != sea::domain::RelationKind::ManyToMany) {
                                    continue;
                                }

                                if (relation.pivot_table.empty() ||
                                    relation.source_fk_column.empty() ||
                                    relation.target_fk_column.empty()) {
                                    std::cerr << "[M2M] relation incomplete pour "
                                              << entity_def.name << "." << relation.name << std::endl;
                                    continue;
                                }

                                std::string target_path = "/" + relation.target_entity;
                                target_path[1] = static_cast<char>(
                                    std::tolower(static_cast<unsigned char>(target_path[1]))
                                    );
                                target_path += "s";

                                std::string source_name = entity_def.name;
                                source_name[0] = static_cast<char>(
                                    std::tolower(static_cast<unsigned char>(source_name[0]))
                                    );

                                std::string route_path = target_path + "/filter/with_" + source_name;

                                std::cerr << "[ROUTE] GET " << route_path << "/<id>"
                                          << " -> ListManyToManyHandler" << std::endl;
                                const bool requires_auth = entity_def.options.enable_auth;
                                r.add(
                                    seastar::httpd::operation_type::GET,
                                    seastar::httpd::url(route_path).remainder("id"),
                                    maybe_protect_handler(
                                    std::make_unique<ListManyToManyHandler>(
                                        crud_engine,
                                        relation.pivot_table,
                                        relation.target_entity,
                                        relation.source_fk_column,
                                        relation.target_fk_column
                                        ),
                                        requires_auth,
                                        auth_service)
                                    );
                            }
                        }
                        // =========================================================
                        // PASS 3 : routes avec id
                        // =========================================================
                        for (const auto& route : route_definitions) {
                            if (route.method == sea::application::HttpMethod::Get &&
                                route.operation_name == "get_by_id") {

                                std::string base_path = "/" + route.entity_name;
                                base_path[1] = static_cast<char>(std::tolower(base_path[1]));
                                base_path += "s";

                                std::cerr << "[ROUTE] GET " << base_path << "/<id>"
                                          << " -> GetByIdHandler" << std::endl;

                                const bool requires_auth = entity_requires_auth(service, route.entity_name);

                                r.add(
                                    seastar::httpd::operation_type::GET,
                                    seastar::httpd::url(base_path).remainder("id"),
                                    maybe_protect_handler(
                                        std::make_unique<GetByIdHandler>(crud_engine, route.entity_name),
                                        requires_auth,
                                        auth_service
                                        )
                                    );
                            }

                            if (route.method == sea::application::HttpMethod::Put &&
                                route.operation_name == "update") {
                                std::cerr << "[ROUTE] PUT " << route.path
                                          << " -> UpdateHandler" << std::endl;
                                std::string base_path = "/" + route.entity_name;
                                base_path[1] = static_cast<char>(std::tolower(base_path[1]));
                                base_path += "s";

                                const bool requires_auth = entity_requires_auth(service, route.entity_name);

                                r.add(
                                    seastar::httpd::operation_type::PUT,
                                    seastar::httpd::url(base_path).remainder("id"),
                                    maybe_protect_handler(
                                        std::make_unique<UpdateHandler>(crud_engine, registry, route.entity_name),
                                        requires_auth,
                                        auth_service
                                        )
                                    );
                            }

                            if (route.method == sea::application::HttpMethod::Delete &&
                                route.operation_name == "delete") {
                                std::string base_path = "/" + route.entity_name;
                                base_path[1] = static_cast<char>(std::tolower(base_path[1]));
                                base_path += "s";
                                std::cerr << "[ROUTE] DELETE " << base_path << "/<id>"
                                          << " -> DeleteHandler" << std::endl;

                                const bool requires_auth = entity_requires_auth(service, route.entity_name);

                                r.add(
                                    seastar::httpd::operation_type::DELETE,
                                    seastar::httpd::url(base_path).remainder("id"),
                                    maybe_protect_handler(
                                        std::make_unique<DeleteHandler>(crud_engine, route.entity_name),
                                        requires_auth,
                                        auth_service
                                        )
                                    );
                            }
                        }
                        std::cerr << "[12] routes CRUD ajoutees" << std::endl;
                    }
                    );
            })
            .then([server, service] {
                std::cerr << "[13] avant listen() sur port " << service.port << std::endl;
                return server->listen(seastar::ipv4_addr{service.port});
            })
            .then([] {
                std::cerr << "[14] serveur demarre" << std::endl;
                return seastar::sleep(std::chrono::hours(24 * 365));
            })
            .handle_exception([server](std::exception_ptr ep) {
                try {
                    std::rethrow_exception(ep);
                } catch (const std::exception& e) {
                    std::cerr << "Exception capturee: " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "Exception inconnue capturee." << std::endl;
                }

                return server->stop().handle_exception([](std::exception_ptr) {
                    return seastar::make_ready_future<>();
                });
            });
    });
}