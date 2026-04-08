#include <seastar/core/app-template.hh>
#include <seastar/core/sleep.hh>
#include <seastar/http/handlers.hh>
#include <seastar/http/httpd.hh>

#include <boost/program_options.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

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

namespace bpo = boost::program_options;
using json = nlohmann::json;

namespace {

using sea::application::HttpMethod;
using sea::application::RouteDefinition;
using sea::infrastructure::runtime::DynamicRecord;
using sea::infrastructure::runtime::DynamicValue;
using sea::infrastructure::runtime::GenericCrudEngine;
using sea::infrastructure::runtime::SchemaRuntimeRegistry;

/**
 * @brief Met la première lettre d'une chaîne en minuscule.
 *
 * Cette fonction est utilisée pour harmoniser la génération des chemins HTTP
 * à partir des noms d'entités (`Department` -> `department`).
 *
 * @param value Chaîne à transformer.
 * @return std::string Chaîne transformée.
 */
[[nodiscard]] std::string lower_first(std::string value) {
    if (!value.empty()) {
        value[0] = static_cast<char>(
            std::tolower(static_cast<unsigned char>(value[0]))
            );
    }
    return value;
}

/**
 * @brief Extrait la partie de base d'une route avec identifiant.
 *
 * Exemple :
 * - `/departments/{id}` -> `/departments`
 *
 * Si `/{id}` n'est pas trouvé, la route est retournée telle quelle.
 *
 * @param path Chemin logique issu du RouteGenerator.
 * @return std::string Chemin de base utilisable avec `remainder("id")`.
 */
[[nodiscard]] std::string base_path_without_id_suffix(const std::string& path) {
    static constexpr std::string_view suffix = "/{id}";
    const auto pos = path.rfind(suffix);
    if (pos == std::string::npos) {
        return path;
    }
    return path.substr(0, pos);
}

/**
 * @brief Échappe les caractères spéciaux pour produire une chaîne JSON valide.
 *
 * @param input Chaîne source brute.
 * @return std::string Chaîne échappée.
 */
[[nodiscard]] std::string json_escape(const std::string& input) {
    std::string out;
    out.reserve(input.size());

    for (char c : input) {
        switch (c) {
        case '"': out += "\\\""; break;
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
 * @brief Convertit une valeur dynamique en fragment JSON.
 *
 * @param value Valeur dynamique à convertir.
 * @return std::string Représentation JSON textuelle.
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
 * @brief Convertit un enregistrement dynamique en objet JSON.
 *
 * @param record Enregistrement sous forme clé/valeur.
 * @return std::string Objet JSON sérialisé.
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
 * @param records Liste des enregistrements à sérialiser.
 * @return std::string Tableau JSON sérialisé.
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

/**
 * @brief Génère un identifiant entier auto-incrémenté pour une entité mémoire.
 *
 * @param entity_name Nom de l'entité ciblée.
 * @param crud_engine Moteur CRUD utilisé pour lire les enregistrements existants.
 * @return std::int64_t Nouvel identifiant.
 */
[[nodiscard]] std::int64_t generate_int_id(
    const std::string& entity_name,
    const std::shared_ptr<GenericCrudEngine>& crud_engine)
{
    const auto records = crud_engine->list(entity_name);
    std::int64_t max_id = 0;

    for (const auto& record : records) {
        const auto it = record.find("id");
        if (it != record.end() && std::holds_alternative<std::int64_t>(it->second)) {
            max_id = std::max(max_id, std::get<std::int64_t>(it->second));
        }
    }

    return max_id + 1;
}

/**
 * @brief Génère un UUID v4 pseudo-aléatoire.
 *
 * @return std::string UUID formaté.
 */
[[nodiscard]] std::string generate_uuid() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<std::uint64_t> dis;

    std::uint64_t part1 = dis(gen);
    std::uint64_t part2 = dis(gen);

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
 * @brief Compare une valeur dynamique à une représentation texte.
 *
 * @param value Valeur dynamique stockée.
 * @param expected Valeur attendue sous forme texte.
 * @return true si les deux correspondent, false sinon.
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
            return std::get<bool>(value);
        }
        if (expected == "false") {
            return !std::get<bool>(value);
        }
    }

    return false;
}

/**
 * @brief Convertit une valeur dynamique en chaîne lorsque cela est possible.
 *
 * @param value Valeur à convertir.
 * @return std::optional<std::string> Valeur convertie ou `nullopt`.
 */
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

/**
 * @brief Convertit une valeur dynamique en identifiant texte.
 *
 * Seuls les types `string` et `int64` sont acceptés ici pour les ids.
 *
 * @param value Valeur à convertir.
 * @return std::optional<std::string> Identifiant converti ou `nullopt`.
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

/**
 * @brief Recherche le premier enregistrement correspondant à un champ donné.
 *
 * @param records Liste d'enregistrements à parcourir.
 * @param field_name Nom du champ à comparer.
 * @param expected_value Valeur attendue.
 * @return std::optional<DynamicRecord> Enregistrement trouvé ou `nullopt`.
 */
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

/**
 * @brief Extrait un token Bearer depuis l'en-tête Authorization.
 *
 * @param req Requête HTTP entrante.
 * @return std::optional<std::string> Token extrait ou `nullopt`.
 */
[[nodiscard]] std::optional<std::string> extract_bearer_token(const seastar::http::request& req) {
    const auto auth_header = req.get_header("Authorization");
    if (auth_header.empty()) {
        return std::nullopt;
    }

    static const std::string prefix = "Bearer ";
    const std::string header_value = auth_header;

    if (header_value.rfind(prefix, 0) != 0) {
        return std::nullopt;
    }

    return header_value.substr(prefix.size());
}

/**
 * @brief Lit une variable d'environnement ou renvoie une valeur de repli.
 *
 * @param name Nom de la variable d'environnement.
 * @param default_value Valeur de secours.
 * @return std::string Valeur trouvée.
 */
[[nodiscard]] std::string get_env_or_default(const char* name, const std::string& default_value) {
    const char* value = std::getenv(name);
    if (value == nullptr || std::string(value).empty()) {
        return default_value;
    }
    return std::string(value);
}

/**
 * @brief Indique si une entité du service exige un jeton JWT.
 *
 * @param service Service courant.
 * @param entity_name Nom de l'entité.
 * @return true si l'entité exige une authentification, false sinon.
 */
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
 * @brief Détermine si une route logique correspond à une opération CRUD standard.
 *
 * @param route Définition de route.
 * @return true si la route est une opération CRUD gérée dans les passes 1 et 3.
 */
[[nodiscard]] bool is_crud_route(const RouteDefinition& route) {
    return route.operation_name == "list"
           || route.operation_name == "create"
           || route.operation_name == "get_by_id"
           || route.operation_name == "update"
           || route.operation_name == "delete";
}

/**
 * @brief Vérifie si une route logique appartient au bloc d'authentification global.
 *
 * @param route Route à inspecter.
 * @return true si la route est `/auth/*`, false sinon.
 */
[[nodiscard]] bool is_auth_route(const RouteDefinition& route) {
    return route.entity_name == "Auth";
}

/**
 * @brief Convertit une méthode HTTP logique vers Seastar.
 *
 * @param method Méthode logique issue du RouteGenerator.
 * @return std::optional<seastar::httpd::operation_type> Méthode Seastar si connue.
 */
[[nodiscard]] std::optional<seastar::httpd::operation_type> to_seastar_operation(HttpMethod method) {
    switch (method) {
    case HttpMethod::Get:
        return seastar::httpd::operation_type::GET;
    case HttpMethod::Post:
        return seastar::httpd::operation_type::POST;
    case HttpMethod::Put:
        return seastar::httpd::operation_type::PUT;
    case HttpMethod::Delete:
        return seastar::httpd::operation_type::DELETE;
    default:
        return std::nullopt;
    }
}

/**
 * @brief Ajoute un handler protégé par JWT si l'authentification est requise.
 *
 * @param handler Handler à enregistrer.
 * @param requires_auth Indique si la route exige un token.
 * @param auth_service Service d'authentification.
 * @return seastar::httpd::handler_base* Pointeur brut attendu par Seastar.
 */
[[nodiscard]] seastar::httpd::handler_base* maybe_protect_handler(
    std::unique_ptr<seastar::httpd::handler_base> handler,
    bool requires_auth,
    const std::shared_ptr<sea::application::AuthService>& auth_service);

/**
 * @brief Enregistre une route CRUD sans paramètre de chemin.
 *
 * Cette fonction est utilisée pour les routes `list` et `create`.
 *
 * @param routes Table de routage Seastar.
 * @param route Route logique à enregistrer.
 * @param crud_engine Moteur CRUD.
 * @param registry Registre runtime du schéma.
 * @param service Service courant.
 * @param auth_service Service d'authentification.
 */
void register_collection_route(
    seastar::httpd::routes& routes,
    const RouteDefinition& route,
    const std::shared_ptr<GenericCrudEngine>& crud_engine,
    const std::shared_ptr<SchemaRuntimeRegistry>& registry,
    const sea::domain::Service& service,
    const std::shared_ptr<sea::application::AuthService>& auth_service);

/**
 * @brief Enregistre une route CRUD avec `{id}` en chemin.
 *
 * Cette fonction est utilisée pour les routes `get_by_id`, `update` et `delete`.
 *
 * @param routes Table de routage Seastar.
 * @param route Route logique à enregistrer.
 * @param crud_engine Moteur CRUD.
 * @param registry Registre runtime du schéma.
 * @param service Service courant.
 * @param auth_service Service d'authentification.
 */
void register_item_route(
    seastar::httpd::routes& routes,
    const RouteDefinition& route,
    const std::shared_ptr<GenericCrudEngine>& crud_engine,
    const std::shared_ptr<SchemaRuntimeRegistry>& registry,
    const sea::domain::Service& service,
    const std::shared_ptr<sea::application::AuthService>& auth_service);

/**
 * @brief Enregistre toutes les routes de relation `HasMany` du service.
 *
 * @param routes Table de routage Seastar.
 * @param crud_engine Moteur CRUD.
 * @param service Service courant.
 * @param auth_service Service d'authentification.
 */
void register_has_many_routes(
    seastar::httpd::routes& routes,
    const std::shared_ptr<GenericCrudEngine>& crud_engine,
    const sea::domain::Service& service,
    const std::shared_ptr<sea::application::AuthService>& auth_service);

/**
 * @brief Enregistre toutes les routes de relation `HasOne` du service.
 *
 * @param routes Table de routage Seastar.
 * @param crud_engine Moteur CRUD.
 * @param service Service courant.
 * @param auth_service Service d'authentification.
 */
void register_has_one_routes(
    seastar::httpd::routes& routes,
    const std::shared_ptr<GenericCrudEngine>& crud_engine,
    const sea::domain::Service& service,
    const std::shared_ptr<sea::application::AuthService>& auth_service);

/**
 * @brief Enregistre toutes les routes de relation `ManyToMany` du service.
 *
 * @param routes Table de routage Seastar.
 * @param crud_engine Moteur CRUD.
 * @param service Service courant.
 * @param auth_service Service d'authentification.
 */
void register_many_to_many_routes(
    seastar::httpd::routes& routes,
    const std::shared_ptr<GenericCrudEngine>& crud_engine,
    const sea::domain::Service& service,
    const std::shared_ptr<sea::application::AuthService>& auth_service);

/**
 * @brief Journalise l'ensemble des routes logiques générées.
 *
 * @param service_name Nom du service courant.
 * @param route_definitions Liste des routes logiques.
 */
void log_route_definitions(
    const std::string& service_name,
    const std::vector<RouteDefinition>& route_definitions);

/**
 * @brief Handler HTTP de registration utilisateur.
 */
class RegisterHandler final : public seastar::httpd::handler_base {
public:
    /**
     * @brief Construit le handler d'inscription.
     *
     * @param crud_engine Moteur CRUD.
     * @param registry Registre du schéma runtime.
     * @param auth_service Service d'authentification.
     * @param db_type Type de base de données du service.
     */
    RegisterHandler(
        std::shared_ptr<GenericCrudEngine> crud_engine,
        std::shared_ptr<SchemaRuntimeRegistry> registry,
        std::shared_ptr<sea::application::AuthService> auth_service,
        sea::domain::DatabaseType db_type)
        : crud_engine_(std::move(crud_engine))
        , registry_(std::move(registry))
        , auth_service_(std::move(auth_service))
        , db_type_(db_type) {}

    /**
     * @brief Traite une requête POST d'inscription.
     *
     * @param path Chemin HTTP, inutilisé ici.
     * @param req Requête entrante.
     * @param rep Réponse à compléter.
     * @return seastar::future<std::unique_ptr<seastar::http::reply>> Réponse asynchrone.
     */
    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override {
        const auto* entity = registry_->find_entity("User");
        if (entity == nullptr) {
            rep->set_status(seastar::http::reply::status_type::not_found);
            rep->write_body("application/json", json{{"error", "Entite User introuvable."}}.dump());
            co_return std::move(rep);
        }

        try {
            sea::infrastructure::runtime::JsonRecordParser parser;
            auto record = parser.parse(*entity, std::string(req->content));

            const auto all_users = crud_engine_->list("User");

            const auto email_it = record.find("email");
            if (email_it == record.end()) {
                rep->set_status(seastar::http::reply::status_type::bad_request);
                rep->write_body("application/json", json{{"error", "Champ email manquant."}}.dump());
                co_return std::move(rep);
            }

            const auto email = dynamic_value_to_string(email_it->second);
            if (!email.has_value()) {
                rep->set_status(seastar::http::reply::status_type::bad_request);
                rep->write_body("application/json", json{{"error", "Email invalide."}}.dump());
                co_return std::move(rep);
            }

            if (find_record_by_field(all_users, "email", *email).has_value()) {
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

            const auto plain_password = dynamic_value_to_string(password_it->second);
            if (!plain_password.has_value()) {
                rep->set_status(seastar::http::reply::status_type::bad_request);
                rep->write_body("application/json", json{{"error", "Password invalide."}}.dump());
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

                if (id_field != nullptr) {
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
                rep->write_body("application/json", json{{"error", "Impossible de creer l'utilisateur."}}.dump());
                co_return std::move(rep);
            }

            json user_json = json::parse(record_to_json(*result.record));
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

private:
    std::shared_ptr<GenericCrudEngine> crud_engine_;
    std::shared_ptr<SchemaRuntimeRegistry> registry_;
    std::shared_ptr<sea::application::AuthService> auth_service_;
    sea::domain::DatabaseType db_type_;
};

/**
 * @brief Handler HTTP de connexion utilisateur.
 */
class LoginHandler final : public seastar::httpd::handler_base {
public:
    /**
     * @brief Construit le handler de connexion.
     *
     * @param crud_engine Moteur CRUD.
     * @param auth_service Service d'authentification.
     */
    LoginHandler(
        std::shared_ptr<GenericCrudEngine> crud_engine,
        std::shared_ptr<sea::application::AuthService> auth_service)
        : crud_engine_(std::move(crud_engine))
        , auth_service_(std::move(auth_service)) {}

    /**
     * @brief Traite une requête POST de connexion.
     *
     * @param path Chemin HTTP, inutilisé ici.
     * @param req Requête entrante.
     * @param rep Réponse à compléter.
     * @return seastar::future<std::unique_ptr<seastar::http::reply>> Réponse asynchrone.
     */
    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override {
        try {
            const auto body = json::parse(std::string(req->content));

            if (!body.contains("email") || !body.contains("password")) {
                rep->set_status(seastar::http::reply::status_type::bad_request);
                rep->write_body("application/json", json{{"error", "email et password sont requis."}}.dump());
                co_return std::move(rep);
            }

            const auto email = body["email"].get<std::string>();
            const auto password = body["password"].get<std::string>();

            const auto users = crud_engine_->list("User");
            const auto user_record = find_record_by_field(users, "email", email);

            if (!user_record.has_value()) {
                rep->set_status(seastar::http::reply::status_type::unauthorized);
                rep->write_body("application/json", json{{"error", "Identifiants invalides."}}.dump());
                co_return std::move(rep);
            }

            const auto pwd_it = user_record->find("password");
            if (pwd_it == user_record->end()) {
                rep->set_status(seastar::http::reply::status_type::unauthorized);
                rep->write_body("application/json", json{{"error", "Identifiants invalides."}}.dump());
                co_return std::move(rep);
            }

            const auto stored_hash = dynamic_value_to_string(pwd_it->second);
            if (!stored_hash.has_value() || !auth_service_->verify_password(password, *stored_hash)) {
                rep->set_status(seastar::http::reply::status_type::unauthorized);
                rep->write_body("application/json", json{{"error", "Identifiants invalides."}}.dump());
                co_return std::move(rep);
            }

            const auto id_it = user_record->find("id");
            if (id_it == user_record->end()) {
                rep->set_status(seastar::http::reply::status_type::internal_server_error);
                rep->write_body("application/json", json{{"error", "Utilisateur invalide."}}.dump());
                co_return std::move(rep);
            }

            const auto user_id = dynamic_value_to_string_id(id_it->second);
            if (!user_id.has_value()) {
                rep->set_status(seastar::http::reply::status_type::internal_server_error);
                rep->write_body("application/json", json{{"error", "ID utilisateur invalide."}}.dump());
                co_return std::move(rep);
            }

            const auto token = auth_service_->generate_token(*user_id, email);

            json user_json = json::parse(record_to_json(*user_record));
            user_json.erase("password");

            rep->set_status(seastar::http::reply::status_type::ok);
            rep->write_body("application/json", json{{"token", token}, {"user", user_json}}.dump());
            co_return std::move(rep);
        } catch (const std::exception& e) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("application/json", json{{"error", std::string("Erreur login: ") + e.what()}}.dump());
            co_return std::move(rep);
        }
    }

private:
    std::shared_ptr<GenericCrudEngine> crud_engine_;
    std::shared_ptr<sea::application::AuthService> auth_service_;
};

/**
 * @brief Handler HTTP retournant l'utilisateur courant à partir du token JWT.
 */
class MeHandler final : public seastar::httpd::handler_base {
public:
    /**
     * @brief Construit le handler /auth/me.
     *
     * @param crud_engine Moteur CRUD.
     * @param auth_service Service d'authentification.
     */
    MeHandler(
        std::shared_ptr<GenericCrudEngine> crud_engine,
        std::shared_ptr<sea::application::AuthService> auth_service)
        : crud_engine_(std::move(crud_engine))
        , auth_service_(std::move(auth_service)) {}

    /**
     * @brief Traite une requête GET pour récupérer l'utilisateur courant.
     *
     * @param path Chemin HTTP, inutilisé ici.
     * @param req Requête entrante.
     * @param rep Réponse à compléter.
     * @return seastar::future<std::unique_ptr<seastar::http::reply>> Réponse asynchrone.
     */
    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override {
        const auto token = extract_bearer_token(*req);
        if (!token.has_value()) {
            rep->set_status(seastar::http::reply::status_type::unauthorized);
            rep->write_body("application/json", json{{"error", "Token manquant."}}.dump());
            co_return std::move(rep);
        }

        const auto claims = auth_service_->verify_token(*token);
        if (!claims.has_value()) {
            rep->set_status(seastar::http::reply::status_type::unauthorized);
            rep->write_body("application/json", json{{"error", "Token invalide."}}.dump());
            co_return std::move(rep);
        }

        const auto user = crud_engine_->get_by_id("User", claims->user_id);
        if (!user.has_value()) {
            rep->set_status(seastar::http::reply::status_type::not_found);
            rep->write_body("application/json", json{{"error", "Utilisateur introuvable."}}.dump());
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

/**
 * @brief Décorateur de handler qui impose une authentification Bearer.
 */
class ProtectedHandler final : public seastar::httpd::handler_base {
public:
    /**
     * @brief Construit un décorateur de protection JWT.
     *
     * @param inner Handler réel à appeler si le token est valide.
     * @param auth_service Service d'authentification.
     */
    ProtectedHandler(
        std::unique_ptr<seastar::httpd::handler_base> inner,
        std::shared_ptr<sea::application::AuthService> auth_service)
        : inner_(std::move(inner))
        , auth_service_(std::move(auth_service)) {}

    /**
     * @brief Valide le token JWT puis délègue au handler interne.
     *
     * @param path Chemin HTTP courant.
     * @param req Requête entrante.
     * @param rep Réponse à compléter.
     * @return seastar::future<std::unique_ptr<seastar::http::reply>> Réponse asynchrone.
     */
    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring& path,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override {
        const auto token = extract_bearer_token(*req);
        if (!token.has_value()) {
            rep->set_status(seastar::http::reply::status_type::unauthorized);
            rep->write_body("application/json", json{{"error", "Token manquant."}}.dump());
            co_return std::move(rep);
        }

        const auto claims = auth_service_->verify_token(*token);
        if (!claims.has_value()) {
            rep->set_status(seastar::http::reply::status_type::unauthorized);
            rep->write_body("application/json", json{{"error", "Token invalide."}}.dump());
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

/**
 * @brief Handler de vérification de santé du service.
 */
class HealthHandler final : public seastar::httpd::handler_base {
public:
    /**
     * @brief Retourne un état simple du service.
     *
     * @param path Chemin HTTP, inutilisé.
     * @param req Requête entrante, inutilisée.
     * @param rep Réponse à compléter.
     * @return seastar::future<std::unique_ptr<seastar::http::reply>> Réponse asynchrone.
     */
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
 * @brief Handler qui expose le document OpenAPI courant.
 */
class OpenApiHandler final : public seastar::httpd::handler_base {
public:
    /**
     * @brief Construit le handler OpenAPI.
     *
     * @param body Contenu JSON du document OpenAPI.
     */
    explicit OpenApiHandler(std::string body)
        : body_(std::move(body)) {}

    /**
     * @brief Retourne le document OpenAPI sérialisé.
     *
     * @param path Chemin HTTP, inutilisé.
     * @param req Requête entrante, inutilisée.
     * @param rep Réponse à compléter.
     * @return seastar::future<std::unique_ptr<seastar::http::reply>> Réponse asynchrone.
     */
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
 * @brief Handler qui expose une page Swagger UI pointant vers /openapi.json.
 */
class SwaggerUiHandler final : public seastar::httpd::handler_base {
public:
    /**
     * @brief Traite une requête GET sur la documentation HTML.
     *
     * @param path Chemin HTTP, inutilisé.
     * @param req Requête entrante, inutilisée.
     * @param rep Réponse à compléter.
     * @return seastar::future<std::unique_ptr<seastar::http::reply>> Réponse asynchrone.
     */
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
        layout: 'StandaloneLayout'
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
 * @brief Handler retournant la liste complète d'une entité.
 */
class ListHandler final : public seastar::httpd::handler_base {
public:
    /**
     * @brief Construit le handler de listing.
     *
     * @param crud_engine Moteur CRUD.
     * @param entity_name Nom de l'entité à lister.
     */
    ListHandler(std::shared_ptr<GenericCrudEngine> crud_engine, std::string entity_name)
        : crud_engine_(std::move(crud_engine))
        , entity_name_(std::move(entity_name)) {}

    /**
     * @brief Retourne tous les enregistrements de l'entité ciblée.
     *
     * @param path Chemin HTTP, inutilisé.
     * @param req Requête entrante, inutilisée.
     * @param rep Réponse à compléter.
     * @return seastar::future<std::unique_ptr<seastar::http::reply>> Réponse asynchrone.
     */
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
 * @brief Handler retournant un parent enrichi de ses enfants.
 */
class GetWithChildrenHandler final : public seastar::httpd::handler_base {
public:
    /**
     * @brief Construit le handler de récupération parent + enfants.
     *
     * @param crud_engine Moteur CRUD.
     * @param parent_entity Entité parent.
     * @param child_entity Entité enfant.
     * @param fk_column Nom de la clé étrangère.
     * @param children_key Clé JSON de la collection enfant dans la réponse.
     */
    GetWithChildrenHandler(
        std::shared_ptr<GenericCrudEngine> crud_engine,
        std::string parent_entity,
        std::string child_entity,
        std::string fk_column,
        std::string children_key)
        : crud_engine_(std::move(crud_engine))
        , parent_entity_(std::move(parent_entity))
        , child_entity_(std::move(child_entity))
        , fk_column_(std::move(fk_column))
        , children_key_(std::move(children_key)) {}

    /**
     * @brief Retourne un parent avec tous ses enfants associés.
     *
     * @param path Chemin HTTP, inutilisé.
     * @param req Requête entrante.
     * @param rep Réponse à compléter.
     * @return seastar::future<std::unique_ptr<seastar::http::reply>> Réponse asynchrone.
     */
    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override {
        const auto parent_id = std::string(req->get_path_param("id"));
        if (parent_id.empty()) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("application/json", json{{"error", "Parametre 'id' manquant."}}.dump());
            co_return std::move(rep);
        }

        const auto parent = crud_engine_->get_by_id(parent_entity_, parent_id);
        if (!parent.has_value()) {
            rep->set_status(seastar::http::reply::status_type::not_found);
            rep->write_body("application/json", json{{"error", parent_entity_ + " introuvable."}}.dump());
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
 * @brief Handler de listing par clé étrangère.
 */
class ListByFkHandler final : public seastar::httpd::handler_base {
public:
    /**
     * @brief Construit le handler de listing par FK.
     *
     * @param crud_engine Moteur CRUD.
     * @param child_entity Entité enfant ciblée.
     * @param fk_column Colonne de clé étrangère.
     */
    ListByFkHandler(
        std::shared_ptr<GenericCrudEngine> crud_engine,
        std::string child_entity,
        std::string fk_column)
        : crud_engine_(std::move(crud_engine))
        , child_entity_(std::move(child_entity))
        , fk_column_(std::move(fk_column)) {}

    /**
     * @brief Retourne tous les enfants liés à un parent donné.
     *
     * @param path Chemin HTTP, inutilisé.
     * @param req Requête entrante.
     * @param rep Réponse à compléter.
     * @return seastar::future<std::unique_ptr<seastar::http::reply>> Réponse asynchrone.
     */
    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override {
        const auto parent_id = std::string(req->get_path_param("id"));
        if (parent_id.empty()) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("application/json", json{{"error", "Parametre 'id' manquant."}}.dump());
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
 * @brief Handler de listing enfant à partir d'un champ unique du parent.
 */
class ListByFkFieldHandler final : public seastar::httpd::handler_base {
public:
    /**
     * @brief Construit le handler de listing par champ parent unique.
     *
     * @param crud_engine Moteur CRUD.
     * @param child_entity Entité enfant.
     * @param parent_entity Entité parent.
     * @param fk_column Colonne de clé étrangère.
     * @param search_field Champ unique du parent à utiliser pour la recherche.
     */
    ListByFkFieldHandler(
        std::shared_ptr<GenericCrudEngine> crud_engine,
        std::string child_entity,
        std::string parent_entity,
        std::string fk_column,
        std::string search_field)
        : crud_engine_(std::move(crud_engine))
        , child_entity_(std::move(child_entity))
        , parent_entity_(std::move(parent_entity))
        , fk_column_(std::move(fk_column))
        , search_field_(std::move(search_field)) {}

    /**
     * @brief Retourne les enfants d'un parent retrouvé via un champ unique.
     *
     * @param path Chemin HTTP, inutilisé.
     * @param req Requête entrante.
     * @param rep Réponse à compléter.
     * @return seastar::future<std::unique_ptr<seastar::http::reply>> Réponse asynchrone.
     */
    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override {
        const auto search_value = std::string(req->get_path_param(search_field_));

        if (search_value.empty()) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body(
                "application/json",
                json{{"error", "Parametre '" + search_field_ + "' manquant."}}.dump());
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
                json{{"error", parent_entity_ + " introuvable avec " + search_field_ + "=" + search_value}}.dump());
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
 * @brief Handler de récupération d'un enregistrement par id.
 */
class GetByIdHandler final : public seastar::httpd::handler_base {
public:
    /**
     * @brief Construit le handler de lecture par identifiant.
     *
     * @param crud_engine Moteur CRUD.
     * @param entity_name Entité ciblée.
     */
    GetByIdHandler(std::shared_ptr<GenericCrudEngine> crud_engine, std::string entity_name)
        : crud_engine_(std::move(crud_engine))
        , entity_name_(std::move(entity_name)) {}

    /**
     * @brief Retourne l'enregistrement correspondant à l'id de l'URL.
     *
     * @param path Chemin HTTP, inutilisé.
     * @param req Requête entrante.
     * @param rep Réponse à compléter.
     * @return seastar::future<std::unique_ptr<seastar::http::reply>> Réponse asynchrone.
     */
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
 * @brief Handler de récupération d'un enfant unique via clé étrangère.
 */
class GetOneByFkHandler final : public seastar::httpd::handler_base {
public:
    /**
     * @brief Construit le handler `has_one`.
     *
     * @param crud_engine Moteur CRUD.
     * @param child_entity Entité enfant ciblée.
     * @param fk_column Colonne de clé étrangère.
     */
    GetOneByFkHandler(
        std::shared_ptr<GenericCrudEngine> crud_engine,
        std::string child_entity,
        std::string fk_column)
        : crud_engine_(std::move(crud_engine))
        , child_entity_(std::move(child_entity))
        , fk_column_(std::move(fk_column)) {}

    /**
     * @brief Retourne l'enfant unique lié à l'id parent fourni.
     *
     * @param path Chemin HTTP, inutilisé.
     * @param req Requête entrante.
     * @param rep Réponse à compléter.
     * @return seastar::future<std::unique_ptr<seastar::http::reply>> Réponse asynchrone.
     */
    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override {
        const auto parent_id = std::string(req->get_path_param("id"));
        if (parent_id.empty()) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("application/json", json{{"error", "Parametre 'id' manquant."}}.dump());
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
        rep->write_body("application/json", json{{"error", "Enregistrement lie introuvable."}}.dump());
        co_return std::move(rep);
    }

private:
    std::shared_ptr<GenericCrudEngine> crud_engine_;
    std::string child_entity_;
    std::string fk_column_;
};

/**
 * @brief Handler de listing d'une relation many-to-many.
 */
class ListManyToManyHandler final : public seastar::httpd::handler_base {
public:
    /**
     * @brief Construit le handler many-to-many.
     *
     * @param crud_engine Moteur CRUD.
     * @param pivot_table Entité pivot.
     * @param target_entity Entité cible.
     * @param source_fk_column FK vers l'entité source.
     * @param target_fk_column FK vers l'entité cible.
     */
    ListManyToManyHandler(
        std::shared_ptr<GenericCrudEngine> crud_engine,
        std::string pivot_table,
        std::string target_entity,
        std::string source_fk_column,
        std::string target_fk_column)
        : crud_engine_(std::move(crud_engine))
        , pivot_table_(std::move(pivot_table))
        , target_entity_(std::move(target_entity))
        , source_fk_column_(std::move(source_fk_column))
        , target_fk_column_(std::move(target_fk_column)) {}

    /**
     * @brief Retourne toutes les cibles liées à l'id source donné.
     *
     * @param path Chemin HTTP, inutilisé.
     * @param req Requête entrante.
     * @param rep Réponse à compléter.
     * @return seastar::future<std::unique_ptr<seastar::http::reply>> Réponse asynchrone.
     */
    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override {
        const auto source_id = std::string(req->get_path_param("id"));
        if (source_id.empty()) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body("application/json", json{{"error", "Parametre 'id' manquant."}}.dump());
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
 * @brief Handler de création générique d'un enregistrement.
 */
class CreateHandler final : public seastar::httpd::handler_base {
public:
    /**
     * @brief Construit le handler de création.
     *
     * @param crud_engine Moteur CRUD.
     * @param registry Registre runtime du schéma.
     * @param entity_name Entité ciblée.
     * @param db_type Type de base de données du service.
     */
    CreateHandler(
        std::shared_ptr<GenericCrudEngine> crud_engine,
        std::shared_ptr<SchemaRuntimeRegistry> registry,
        std::string entity_name,
        std::shared_ptr<sea::application::AuthService> auth_service,
        sea::domain::DatabaseType db_type)
        : crud_engine_(std::move(crud_engine))
        , registry_(std::move(registry))
        , entity_name_(std::move(entity_name))
        , auth_service_(std::move(auth_service))
        , db_type_(db_type) {}

    /**
     * @brief Traite une requête POST de création.
     *
     * @param path Chemin HTTP, inutilisé.
     * @param req Requête entrante.
     * @param rep Réponse à compléter.
     * @return seastar::future<std::unique_ptr<seastar::http::reply>> Réponse asynchrone.
     */
    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override {
        const auto* entity = registry_->find_entity(entity_name_);
        if (entity == nullptr) {
            rep->set_status(seastar::http::reply::status_type::not_found);
            rep->write_body("text/plain", "Entite inconnue.");
            co_return std::move(rep);
        }

        try {
            sea::infrastructure::runtime::JsonRecordParser parser;
            auto record = parser.parse(*entity, std::string(req->content));
            const auto password_it = record.find("password");
            if (password_it != record.end()) {
                const auto plain_password = dynamic_value_to_string(password_it->second);
                if (!plain_password.has_value()) {
                    rep->set_status(seastar::http::reply::status_type::bad_request);
                    rep->write_body("application/json", json{{"error", "Password invalide."}}.dump());
                    co_return std::move(rep);
                }
                record["password"] = auth_service_->hash_password(*plain_password);
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
    std::shared_ptr<SchemaRuntimeRegistry> registry_;
    std::string entity_name_;
    sea::domain::DatabaseType db_type_;
    std::shared_ptr<sea::application::AuthService> auth_service_;
};

/**
 * @brief Handler de mise à jour générique d'un enregistrement.
 */
class UpdateHandler final : public seastar::httpd::handler_base {
public:
    /**
     * @brief Construit le handler de mise à jour.
     *
     * @param crud_engine Moteur CRUD.
     * @param registry Registre runtime du schéma.
     * @param entity_name Entité ciblée.
     */
    UpdateHandler(
        std::shared_ptr<GenericCrudEngine> crud_engine,
        std::shared_ptr<SchemaRuntimeRegistry> registry,
        std::string entity_name)
        : crud_engine_(std::move(crud_engine))
        , registry_(std::move(registry))
        , entity_name_(std::move(entity_name)) {}

    /**
     * @brief Traite une requête PUT de mise à jour.
     *
     * @param path Chemin HTTP, inutilisé.
     * @param req Requête entrante.
     * @param rep Réponse à compléter.
     * @return seastar::future<std::unique_ptr<seastar::http::reply>> Réponse asynchrone.
     */
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
        if (entity == nullptr) {
            rep->set_status(seastar::http::reply::status_type::not_found);
            rep->write_body("text/plain", "Entite inconnue.");
            co_return std::move(rep);
        }

        try {
            sea::infrastructure::runtime::JsonRecordParser parser;
            auto record = parser.parse(*entity, std::string(req->content));
            const auto result = crud_engine_->update(entity_name_, std::string(id), std::move(record));

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

            rep->set_status(seastar::http::reply::status_type::ok);
            rep->write_body("application/json", record_to_json(*result.record));
            co_return std::move(rep);
        } catch (const std::exception& e) {
            rep->set_status(seastar::http::reply::status_type::bad_request);
            rep->write_body(
                "application/json",
                json{{"bad_request", std::string("Erreur JSON: ") + e.what()}}.dump());
            co_return std::move(rep);
        }
    }

private:
    std::shared_ptr<GenericCrudEngine> crud_engine_;
    std::shared_ptr<SchemaRuntimeRegistry> registry_;
    std::string entity_name_;
};

/**
 * @brief Handler de suppression générique d'un enregistrement.
 */
class DeleteHandler final : public seastar::httpd::handler_base {
public:
    /**
     * @brief Construit le handler de suppression.
     *
     * @param crud_engine Moteur CRUD.
     * @param entity_name Entité ciblée.
     */
    DeleteHandler(std::shared_ptr<GenericCrudEngine> crud_engine, std::string entity_name)
        : crud_engine_(std::move(crud_engine))
        , entity_name_(std::move(entity_name)) {}

    /**
     * @brief Supprime l'enregistrement identifié par `{id}`.
     *
     * @param path Chemin HTTP, inutilisé.
     * @param req Requête entrante.
     * @param rep Réponse à compléter.
     * @return seastar::future<std::unique_ptr<seastar::http::reply>> Réponse asynchrone.
     */
    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override {
        const auto id = req->get_path_param("id");
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

void register_collection_route(
    seastar::httpd::routes& routes,
    const RouteDefinition& route,
    const std::shared_ptr<GenericCrudEngine>& crud_engine,
    const std::shared_ptr<SchemaRuntimeRegistry>& registry,
    const sea::domain::Service& service,
    const std::shared_ptr<sea::application::AuthService>& auth_service)
{
    if (is_auth_route(route)) {
        return;
    }

    const auto operation = to_seastar_operation(route.method);
    if (!operation.has_value()) {
        return;
    }

    const bool requires_auth = entity_requires_auth(service, route.entity_name);

    if (route.operation_name == "list") {
        std::cerr << "[ROUTE] GET " << route.path << " -> ListHandler" << std::endl;
        routes.add(
            *operation,
            seastar::httpd::url(route.path),
            maybe_protect_handler(
                std::make_unique<ListHandler>(crud_engine, route.entity_name),
                requires_auth,
                auth_service));
        return;
    }

    if (route.operation_name == "create") {
        std::cerr << "[ROUTE] POST " << route.path << " -> CreateHandler" << std::endl;
        routes.add(
            *operation,
            seastar::httpd::url(route.path),
            maybe_protect_handler(
                std::make_unique<CreateHandler>(
                    crud_engine,
                    registry,
                    route.entity_name,
                    auth_service,
                    service.database_config.type),
                requires_auth,
                auth_service));
    }
}

void register_item_route(
    seastar::httpd::routes& routes,
    const RouteDefinition& route,
    const std::shared_ptr<GenericCrudEngine>& crud_engine,
    const std::shared_ptr<SchemaRuntimeRegistry>& registry,
    const sea::domain::Service& service,
    const std::shared_ptr<sea::application::AuthService>& auth_service)
{
    if (is_auth_route(route)) {
        return;
    }

    if (!is_crud_route(route)) {
        return;
    }

    const auto operation = to_seastar_operation(route.method);
    if (!operation.has_value()) {
        return;
    }

    const bool requires_auth = entity_requires_auth(service, route.entity_name);
    const auto base_path = base_path_without_id_suffix(route.path);

    if (route.operation_name == "get_by_id") {
        std::cerr << "[ROUTE] GET " << route.path << " -> GetByIdHandler" << std::endl;
        routes.add(
            *operation,
            seastar::httpd::url(base_path).remainder("id"),
            maybe_protect_handler(
                std::make_unique<GetByIdHandler>(crud_engine, route.entity_name),
                requires_auth,
                auth_service));
        return;
    }

    if (route.operation_name == "update") {
        std::cerr << "[ROUTE] PUT " << route.path << " -> UpdateHandler" << std::endl;
        routes.add(
            *operation,
            seastar::httpd::url(base_path).remainder("id"),
            maybe_protect_handler(
                std::make_unique<UpdateHandler>(crud_engine, registry, route.entity_name),
                requires_auth,
                auth_service));
        return;
    }

    if (route.operation_name == "delete") {
        std::cerr << "[ROUTE] DELETE " << route.path << " -> DeleteHandler" << std::endl;
        routes.add(
            *operation,
            seastar::httpd::url(base_path).remainder("id"),
            maybe_protect_handler(
                std::make_unique<DeleteHandler>(crud_engine, route.entity_name),
                requires_auth,
                auth_service));
    }
}

void register_has_many_routes(
    seastar::httpd::routes& routes,
    const std::shared_ptr<GenericCrudEngine>& crud_engine,
    const sea::domain::Service& service,
    const std::shared_ptr<sea::application::AuthService>& auth_service)
{
    for (const auto& entity_def : service.schema.entities) {
        for (const auto& relation : entity_def.relations) {
            if (relation.kind != sea::domain::RelationKind::HasMany) {
                continue;
            }

            std::string child_path = "/" + lower_first(relation.target_entity) + "s";
            std::string parent_name = lower_first(entity_def.name);
            const bool requires_auth = entity_def.options.enable_auth;

            const std::string route_path = child_path + "/filter/with_" + parent_name;
            std::cerr << "[ROUTE] GET " << route_path << "/<id> -> ListByFkHandler" << std::endl;
            routes.add(
                seastar::httpd::operation_type::GET,
                seastar::httpd::url(route_path).remainder("id"),
                maybe_protect_handler(
                    std::make_unique<ListByFkHandler>(crud_engine, relation.target_entity, relation.fk_column),
                    requires_auth,
                    auth_service));

            for (const auto& field : entity_def.fields) {
                if (!field.unique || field.name == "id") {
                    continue;
                }

                const std::string route_by_field =
                    child_path + "/filter/with_" + parent_name + "_" + field.name;

                std::cerr << "[ROUTE] GET " << route_by_field << "/<" << field.name
                          << "> -> ListByFkFieldHandler" << std::endl;

                routes.add(
                    seastar::httpd::operation_type::GET,
                    seastar::httpd::url(route_by_field).remainder(field.name),
                    maybe_protect_handler(
                        std::make_unique<ListByFkFieldHandler>(
                            crud_engine,
                            relation.target_entity,
                            entity_def.name,
                            relation.fk_column,
                            field.name),
                        requires_auth,
                        auth_service));
            }

            std::string parent_path = "/" + lower_first(entity_def.name) + "s";
            const std::string route_with_children = parent_path + "_with_" + relation.name;

            std::cerr << "[ROUTE] GET " << route_with_children << "/<id> -> GetWithChildrenHandler" << std::endl;
            routes.add(
                seastar::httpd::operation_type::GET,
                seastar::httpd::url(route_with_children).remainder("id"),
                maybe_protect_handler(
                    std::make_unique<GetWithChildrenHandler>(
                        crud_engine,
                        entity_def.name,
                        relation.target_entity,
                        relation.fk_column,
                        relation.name),
                    requires_auth,
                    auth_service));
        }
    }
}

void register_has_one_routes(
    seastar::httpd::routes& routes,
    const std::shared_ptr<GenericCrudEngine>& crud_engine,
    const sea::domain::Service& service,
    const std::shared_ptr<sea::application::AuthService>& auth_service)
{
    for (const auto& entity_def : service.schema.entities) {
        for (const auto& relation : entity_def.relations) {
            if (relation.kind != sea::domain::RelationKind::HasOne) {
                continue;
            }

            const std::string parent_path = "/" + lower_first(entity_def.name) + "s";
            const std::string full_path = parent_path + "/" + relation.name;
            const bool requires_auth = entity_def.options.enable_auth;

            std::cerr << "[ROUTE] GET " << full_path << "/<id> -> GetOneByFkHandler" << std::endl;
            routes.add(
                seastar::httpd::operation_type::GET,
                seastar::httpd::url(full_path).remainder("id"),
                maybe_protect_handler(
                    std::make_unique<GetOneByFkHandler>(crud_engine, relation.target_entity, relation.fk_column),
                    requires_auth,
                    auth_service));
        }
    }
}

void register_many_to_many_routes(
    seastar::httpd::routes& routes,
    const std::shared_ptr<GenericCrudEngine>& crud_engine,
    const sea::domain::Service& service,
    const std::shared_ptr<sea::application::AuthService>& auth_service)
{
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

            const std::string target_path = "/" + lower_first(relation.target_entity) + "s";
            const std::string source_name = lower_first(entity_def.name);
            const std::string route_path = target_path + "/filter/with_" + source_name;
            const bool requires_auth = entity_def.options.enable_auth;

            std::cerr << "[ROUTE] GET " << route_path << "/<id> -> ListManyToManyHandler" << std::endl;
            routes.add(
                seastar::httpd::operation_type::GET,
                seastar::httpd::url(route_path).remainder("id"),
                maybe_protect_handler(
                    std::make_unique<ListManyToManyHandler>(
                        crud_engine,
                        relation.pivot_table,
                        relation.target_entity,
                        relation.source_fk_column,
                        relation.target_fk_column),
                    requires_auth,
                    auth_service));
        }
    }
}

void log_route_definitions(
    const std::string& service_name,
    const std::vector<RouteDefinition>& route_definitions)
{
    std::cerr << "========== ROUTES POUR " << service_name << " ==========" << std::endl;

    for (const auto& route : route_definitions) {
        const char* method = "UNKNOWN";
        switch (route.method) {
        case HttpMethod::Get:    method = "GET"; break;
        case HttpMethod::Post:   method = "POST"; break;
        case HttpMethod::Put:    method = "PUT"; break;
        case HttpMethod::Delete: method = "DELETE"; break;
        }

        std::cerr << method << "  " << route.path
                  << "  [" << route.entity_name
                  << " / " << route.operation_name << "]" << std::endl;
    }

    std::cerr << "==========================================" << std::endl;
}

} // namespace

/**
 * @brief Point d'entrée principal du service Seastar.
 *
 * Cette fonction :
 * - lit les arguments `--config` et `--service_name`
 * - charge le projet YAML
 * - valide le service sélectionné
 * - initialise les dépendances runtime
 * - génère l'OpenAPI
 * - enregistre les routes HTTP et démarre le serveur
 *
 * @param argc Nombre d'arguments CLI.
 * @param argv Valeurs des arguments CLI.
 * @return int Code de sortie du processus.
 */
int main(int argc, char** argv) {
    seastar::app_template app;

    app.add_options()
        ("config", bpo::value<std::string>()->required(), "Chemin du fichier YAML")
        ("service_name", bpo::value<std::string>()->required(), "Nom du service a lancer");

    return app.run(argc, argv, [&app] {
        const auto& cfg = app.configuration();
        const std::string config_path = cfg["config"].as<std::string>();
        const std::string service_name = cfg["service_name"].as<std::string>();

        std::cerr << "[BOOT] config = " << config_path << std::endl;
        std::cerr << "[BOOT] service = " << service_name << std::endl;

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

        if (selected_service == nullptr) {
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

        auto registry = std::make_shared<sea::infrastructure::runtime::SchemaRuntimeRegistry>();
        auto repository_factory = std::make_shared<sea::infrastructure::persistence::RepositoryFactory>();

        sea::application::StartServiceUseCase start_usecase(*registry, *repository_factory);
        auto repository = std::shared_ptr<sea::infrastructure::persistence::IGenericRepository>(
            std::move(start_usecase.execute(service))
            );

        auto validator = std::make_shared<sea::infrastructure::runtime::GenericValidator>();
        auto crud_engine = std::make_shared<sea::infrastructure::runtime::GenericCrudEngine>(
            registry,
            validator,
            repository
            );

        sea::application::RouteGenerator route_generator;
        const auto route_definitions = route_generator.generate(service.schema);

        sea::application::OpenApiGenerator openapi_generator;
        const auto openapi_doc = openapi_generator.generate(service, route_definitions);
        const auto openapi_json = openapi_doc.dump(2);

        log_route_definitions(service.name, route_definitions);

        auto server = std::make_shared<seastar::httpd::http_server_control>();
        const auto jwt_secret = get_env_or_default("SEA_JWT_SECRET", "dev_only_secret_change_me");
        auto auth_service = std::make_shared<sea::application::AuthService>(jwt_secret);

        return server->start()
            .then([server, crud_engine, registry, route_definitions, service, openapi_json, auth_service] {
                std::cerr << "[BOOT] serveur HTTP initialise, enregistrement des routes..." << std::endl;

                return server->set_routes(
                    [crud_engine, registry, route_definitions, service, openapi_json, auth_service](seastar::httpd::routes& r) {
                        r.add(
                            seastar::httpd::operation_type::GET,
                            seastar::httpd::url("/health"),
                            new HealthHandler());

                        r.add(
                            seastar::httpd::operation_type::GET,
                            seastar::httpd::url("/openapi.json"),
                            new OpenApiHandler(openapi_json));

                        r.add(
                            seastar::httpd::operation_type::GET,
                            seastar::httpd::url("/docs"),
                            new SwaggerUiHandler());

                        r.add(
                            seastar::httpd::operation_type::POST,
                            seastar::httpd::url("/auth/register"),
                            new RegisterHandler(crud_engine, registry, auth_service, service.database_config.type));

                        r.add(
                            seastar::httpd::operation_type::POST,
                            seastar::httpd::url("/auth/login"),
                            new LoginHandler(crud_engine, auth_service));

                        r.add(
                            seastar::httpd::operation_type::GET,
                            seastar::httpd::url("/auth/me"),
                            new MeHandler(crud_engine, auth_service));

                        for (const auto& route : route_definitions) {
                            if (route.operation_name == "list" || route.operation_name == "create") {
                                register_collection_route(r, route, crud_engine, registry, service, auth_service);
                            }
                        }

                        register_has_many_routes(r, crud_engine, service, auth_service);
                        register_has_one_routes(r, crud_engine, service, auth_service);
                        register_many_to_many_routes(r, crud_engine, service, auth_service);

                        for (const auto& route : route_definitions) {
                            if (route.operation_name == "get_by_id"
                                || route.operation_name == "update"
                                || route.operation_name == "delete") {
                                register_item_route(r, route, crud_engine, registry, service, auth_service);
                            }
                        }

                        std::cerr << "[BOOT] toutes les routes sont enregistrees" << std::endl;
                    });
            })
            .then([server, service] {
                std::cerr << "[BOOT] listen sur port " << service.port << std::endl;
                return server->listen(seastar::ipv4_addr{service.port});
            })
            .then([] {
                std::cerr << "[BOOT] serveur demarre" << std::endl;
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
