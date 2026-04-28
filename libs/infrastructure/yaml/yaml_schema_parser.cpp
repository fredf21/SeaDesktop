#include "yaml_schema_parser.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace sea::infrastructure::yaml {

namespace {

// Convertit une chaîne en minuscules
[[nodiscard]] std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return value;
}

// Lit une valeur optionnelle avec valeur par défaut
template <typename T>
[[nodiscard]] T get_or_default(const YAML::Node& node,
                               const char* key,
                               const T& default_value) {
    if (!node || !node[key]) {
        return default_value;
    }

    try {
        return node[key].as<T>();
    } catch (const YAML::Exception& e) {
        throw std::runtime_error(
            std::string("Valeur invalide pour '") + key + "': " + e.what()
            );
    }
}

} // namespace

bool YamlSchemaParser::has_key(const YAML::Node& node, const char* key) const {
    return node && node[key];
}

std::string YamlSchemaParser::require_string(const YAML::Node& node,
                                             const char* key,
                                             const char* context) const {
    if (!has_key(node, key)) {
        throw std::runtime_error(
            std::string("Champ obligatoire manquant '") + key + "' dans " + context
            );
    }

    try {
        return node[key].as<std::string>();
    } catch (const YAML::Exception& e) {
        throw std::runtime_error(
            std::string("Champ '") + key + "' invalide dans " + context + ": " + e.what()
            );
    }
}

std::string YamlSchemaParser::resolve_env(const std::string &value) const
{
    if (value.size() >= 4 &&
        value[0] == '$' &&
        value[1] == '{' &&
        value.back() == '}') {

        const std::string var_name = value.substr(2, value.size() - 3);
        const char* env_value = std::getenv(var_name.c_str());

        if (env_value == nullptr) {
            throw std::runtime_error(
                "Variable d'environnement manquante: " + var_name
                );
        }

        return std::string(env_value);
    }
    return value;
}

sea::domain::Project YamlSchemaParser::parse_project_file(const std::string& file_path) const {
    YAML::Node root;

    try {
        root = YAML::LoadFile(file_path);
    } catch (const YAML::Exception& e) {
        throw std::runtime_error(
            "Impossible de charger le fichier YAML '" + file_path + "': " + e.what()
            );
    }

    return parse_project_node(root);
}

sea::domain::Service YamlSchemaParser::parse_service_file(const std::string& file_path) const {
    YAML::Node root;

    try {
        root = YAML::LoadFile(file_path);
    } catch (const YAML::Exception& e) {
        throw std::runtime_error(
            "Impossible de charger le fichier YAML '" + file_path + "': " + e.what()
            );
    }

    return parse_service_node(root);
}

sea::domain::Project YamlSchemaParser::parse_project_node(const YAML::Node& root) const {
    if (!root || !root.IsMap()) {
        throw std::runtime_error("Le document YAML racine doit être un objet.");
    }

    sea::domain::Project project{};

    // project:
    //   name: ...
    if (has_key(root, "project")) {
        const YAML::Node project_node = root["project"];

        if (!project_node.IsMap()) {
            throw std::runtime_error("'project' doit être un objet.");
        }

        project.name = require_string(project_node, "name", "project");
    } else {
        project.name = "UnnamedProject";
    }

    // services:
    if (!has_key(root, "services")) {
        throw std::runtime_error("Le champ 'services' est obligatoire à la racine.");
    }

    const YAML::Node services_node = root["services"];
    if (!services_node.IsSequence()) {
        throw std::runtime_error("'services' doit être une liste.");
    }

    for (const auto& service_node : services_node) {
        project.services.push_back(parse_service_node(service_node));
    }

    return project;
}

sea::domain::Service YamlSchemaParser::parse_service_node(const YAML::Node& node) const {
    if (!node || !node.IsMap()) {
        throw std::runtime_error("Un service YAML doit être un objet.");
    }

    sea::domain::Service service{};

    service.name = require_string(node, "name", "service");

    const int raw_port = get_or_default<int>(node, "port", 8080);
    if (raw_port < 1 || raw_port > 65535) {
        throw std::runtime_error(
            "Le port du service '" + service.name + "' doit être entre 1 et 65535."
            );
    }
    service.port = static_cast<std::uint16_t>(raw_port);

    // database:
    if (has_key(node, "database")) {
        const YAML::Node db_node = node["database"];
        if (!db_node.IsMap()) {
            throw std::runtime_error(
                "'database' doit être un objet dans le service '" + service.name + "'."
                );
        }

        service.database_config = parse_database_config_node(db_node);
    }

    if(has_key(node, "security")){
        const YAML::Node security_node = node["security"];
        if(!security_node.IsMap()){
            throw std::runtime_error(
                "'security' doit être un objet dans le service '" + service.name + "'."
                );
        }
        service.security = parse_security_node(security_node);
    }

    else{
        // Pas de section security : defaults sécurisés
        service.security = sea::domain::security::SecurityConfig::safe_defaults();
    }

    // entities:
    if (has_key(node, "entities")) {
        const YAML::Node entities_node = node["entities"];

        if (!entities_node.IsSequence()) {
            throw std::runtime_error(
                "'entities' doit être une liste dans le service '" + service.name + "'."
                );
        }

        for (const auto& entity_node : entities_node) {
            service.schema.entities.push_back(parse_entity_node(entity_node));
        }
    }

    return service;
}


sea::domain::Entity YamlSchemaParser::parse_entity_node(const YAML::Node& node) const {
    if (!node || !node.IsMap()) {
        throw std::runtime_error("Une entité YAML doit être un objet.");
    }

    sea::domain::Entity entity{};

    entity.name = require_string(node, "name", "entity");
    std::string s = entity.name + "s";
    s[0] = static_cast<char>(std::tolower(s[0]));
    entity.table_name = get_or_default<std::string>(node, "table_name", s);

    // options:
    if (has_key(node, "options")) {
        const YAML::Node options_node = node["options"];
        if (!options_node.IsMap()) {
            throw std::runtime_error(
                "'options' doit être un objet dans l'entité '" + entity.name + "'."
                );
        }

        entity.options.enable_crud =
            get_or_default<bool>(options_node, "enable_crud", entity.options.enable_crud);
        entity.options.is_auth_source =
            get_or_default<bool>(options_node, "is_auth_source", entity.options.is_auth_source);
        entity.options.public_routes =
            get_or_default<bool>(options_node, "public_routes", false);
        entity.options.enable_websocket =
            get_or_default<bool>(options_node, "enable_websocket", entity.options.enable_websocket);
        entity.options.soft_delete =
            get_or_default<bool>(options_node, "soft_delete", entity.options.soft_delete);
        entity.options.timestamps =
            get_or_default<bool>(options_node, "timestamps", entity.options.timestamps);
    }

    // fields:
    if (has_key(node, "fields")) {
        const YAML::Node fields_node = node["fields"];

        if (!fields_node.IsSequence()) {
            throw std::runtime_error(
                "'fields' doit être une liste dans l'entité '" + entity.name + "'."
                );
        }

        for (const auto& field_node : fields_node) {
            entity.fields.push_back(parse_field_node(field_node));
        }
    }

    // relations:
    if (has_key(node, "relations")) {
        const YAML::Node relations_node = node["relations"];

        if (!relations_node.IsSequence()) {
            throw std::runtime_error(
                "'relations' doit être une liste dans l'entité '" + entity.name + "'."
                );
        }

        for (const auto& relation_node : relations_node) {
            entity.relations.push_back(parse_relation_node(relation_node));
        }
    }

    return entity;
}

sea::domain::Field YamlSchemaParser::parse_field_node(const YAML::Node& node) const {
    if (!node || !node.IsMap()) {
        throw std::runtime_error("Un champ YAML doit être un objet.");
    }

    sea::domain::Field field{};

    field.name = require_string(node, "name", "field");

    const std::string type_str = require_string(node, "type", "field");
    const auto field_type = sea::domain::field_type_from_string(type_str);
    if (!field_type.has_value()) {
        throw std::runtime_error("Type de champ inconnu: '" + type_str + "'.");
    }
    field.type = *field_type;

    field.required     = get_or_default<bool>(node, "required", field.required);
    field.unique       = get_or_default<bool>(node, "unique", field.unique);
    field.indexed      = get_or_default<bool>(node, "indexed", field.indexed);
    field.serializable = get_or_default<bool>(node, "serializable", field.serializable);

    // Contrainte utile : password non sérialisable par défaut si rien n'est précisé
    if (field.type == sea::domain::FieldType::Password && !has_key(node, "serializable")) {
        field.serializable = false;
    }

    // max_length
    if (has_key(node, "max_length")) {
        try {
            field.max_length = node["max_length"].as<std::size_t>();
        } catch (const YAML::Exception& e) {
            throw std::runtime_error(
                "max_length invalide pour le champ '" + field.name + "': " + e.what()
                );
        }
    }

    // min_value
    if (has_key(node, "min_value")) {
        try {
            if (field.type == sea::domain::FieldType::Float) {
                field.min_value = node["min_value"].as<double>();
            } else {
                field.min_value = node["min_value"].as<std::int64_t>();
            }
        } catch (const YAML::Exception& e) {
            throw std::runtime_error(
                "min_value invalide pour le champ '" + field.name + "': " + e.what()
                );
        }
    }

    // max_value
    if (has_key(node, "max_value")) {
        try {
            if (field.type == sea::domain::FieldType::Float) {
                field.max_value = node["max_value"].as<double>();
            } else {
                field.max_value = node["max_value"].as<std::int64_t>();
            }
        } catch (const YAML::Exception& e) {
            throw std::runtime_error(
                "max_value invalide pour le champ '" + field.name + "': " + e.what()
                );
        }
    }

    // default
    if (has_key(node, "default")) {
        try {
            switch (field.type) {
            case sea::domain::FieldType::String:
            case sea::domain::FieldType::Text:
            case sea::domain::FieldType::UUID:
            case sea::domain::FieldType::Password:
            case sea::domain::FieldType::Email:
            case sea::domain::FieldType::Timestamp:
                field.default_val = node["default"].as<std::string>();
                break;

            case sea::domain::FieldType::Int:
                field.default_val = node["default"].as<std::int64_t>();
                break;

            case sea::domain::FieldType::Float:
                field.default_val = node["default"].as<double>();
                break;

            case sea::domain::FieldType::Bool:
                field.default_val = node["default"].as<bool>();
                break;
            }
        } catch (const YAML::Exception& e) {
            throw std::runtime_error(
                "Valeur par défaut invalide pour le champ '" + field.name + "': " + e.what()
                );
        }
    }

    return field;
}

sea::domain::Relation YamlSchemaParser::parse_relation_node(const YAML::Node& node) const {
    if (!node || !node.IsMap()) {
        throw std::runtime_error("Une relation YAML doit être un objet.");
    }

    sea::domain::Relation relation{};

    relation.name = require_string(node, "name", "relation");
    relation.target_entity = require_string(node, "target_entity", "relation");

    const std::string kind_str = require_string(node, "kind", "relation");
    relation.kind = parse_relation_kind(kind_str);

    if (has_key(node, "on_delete")) {
        const auto on_delete_str = node["on_delete"].as<std::string>();
        relation.on_delete = parse_on_delete(on_delete_str);
    }

    relation.fk_column        = get_or_default<std::string>(node, "fk_column", "");
    relation.pivot_table      = get_or_default<std::string>(node, "pivot_table", "");
    relation.source_fk_column = get_or_default<std::string>(node, "source_fk_column", "");
    relation.target_fk_column = get_or_default<std::string>(node, "target_fk_column", "");
    if (relation.kind == sea::domain::RelationKind::ManyToMany) {
        if (relation.pivot_table.empty()) {
            throw std::runtime_error(
                "La relation many_to_many '" + relation.name +
                "' doit definir 'pivot_table'."
                );
        }

        if (relation.source_fk_column.empty()) {
            throw std::runtime_error(
                "La relation many_to_many '" + relation.name +
                "' doit definir 'source_fk_column'."
                );
        }

        if (relation.target_fk_column.empty()) {
            throw std::runtime_error(
                "La relation many_to_many '" + relation.name +
                "' doit definir 'target_fk_column'."
                );
        }
    }
    return relation;
}

sea::domain::security::SecurityConfig YamlSchemaParser::parse_security_node(const YAML::Node &node) const
{
    using SecurityConfig = sea::domain::security::SecurityConfig;
    if (!node || !node.IsMap()) {
        throw std::runtime_error("Un champ YAML doit être un objet.");
    }
    SecurityConfig security_config = SecurityConfig::safe_defaults();

    // Authentication
    if (const YAML::Node auth_node = node["authentication"]) {
        if (!auth_node.IsMap()) {
            throw std::runtime_error(
                "Le champ 'authentication' doit être un objet dans service '"
                );
        }
        security_config.set_authentication(parse_auth_node(auth_node));
    }

    // Cors
    if (const YAML::Node cors_node = node["cors"]) {
        if (!cors_node.IsMap()) {
            throw std::runtime_error(
                "Le champ 'cors' doit être un objet dans securitE '"
                );
        }
        security_config.set_cors(parse_cors_node(cors_node));
    }

    // Rate Limits
    if (const YAML::Node rate_limits_node = node["rate_limits"]) {
        if (!rate_limits_node.IsSequence()) {
            throw std::runtime_error(
                "Le champ 'rate_limits' doit être une liste dans service '"
                );
        }
        std::vector<domain::security::RateLimitRule> rules;
        for (const auto& rule_node : rate_limits_node) {
            rules.push_back(parse_rate_limite_rule_node(rule_node));
        }
        security_config.set_rate_limits(std::move(rules));
    }

    // Security Headers
    if (const YAML::Node headers_node = node["headers"]) {
        if (!headers_node.IsMap()) {
            throw std::runtime_error(
                "Le champ 'headers' doit être un objet dans service '"
                );
        }
        security_config.set_security_headers(parse_security_headers_node(headers_node));
    }

    // HTTP Limits
    if (const YAML::Node limits_node = node["http_limits"]) {
        if (!limits_node.IsMap()) {
            throw std::runtime_error(
                "Le champ 'http_limits' doit être un objet dans service '"
                );
        }
        security_config.set_http_limits(parse_http_limits_node(limits_node));
    }

    // Validation finale de cohérence
    try {
        security_config.validate();
    } catch (const std::exception& e) {
        throw std::runtime_error(
            "Configuration de sécurité invalide dans service '"
            );
    }

    return security_config;
}
domain::security::AuthentificationConfig YamlSchemaParser::parse_auth_node(const YAML::Node &node) const
{
    if (!node || !node.IsMap()) {
        throw std::runtime_error("Un champ YAML doit être un objet.");
    }
    using AuthentificationConfig = domain::security::AuthentificationConfig;
    AuthentificationConfig auth_config;
    const std::string type_str =
        get_or_default<std::string>(node, "type", "none");
    try {
        auth_config.set_type(domain::security::auth_type_from_string(type_str));
    } catch (const std::exception& e) {
        throw std::runtime_error(
            std::string("Type d'authentification invalide dans service '") + e.what()
            );
    }

    // JWT
    if (auth_config.type() == domain::security::AuthType::Jwt) {
        const std::string algo_str =
            get_or_default<std::string>(node, "algorithm", "HS256");
        try {
            auth_config.set_jwt_algorithm(domain::security::jwt_algorithm_from_string(algo_str));
        } catch (const std::exception& e) {
            throw std::runtime_error(
                std::string("Algorithme JWT invalide dans service '") + e.what()
                );
        }

        if (node["secret"]) {
            auth_config.set_jwt_secret(resolve_env(node["secret"].as<std::string>()));
        }
        if (node["public_key_path"]) {
            auth_config.set_jwt_public_key_path(node["public_key_path"].as<std::string>());
        }
        if (node["private_key_path"]) {
            auth_config.set_jwt_private_key_path(node["private_key_path"].as<std::string>());
        }
        if (node["issuer"]) {
            auth_config.set_jwt_issuer(node["issuer"].as<std::string>());
        }
        if (node["audience"]) {
            auth_config.set_jwt_audience(node["audience"].as<std::string>());
        }
        if (node["access_token_ttl"]) {
            auth_config.set_access_token_ttl(parse_duration(node["access_token_ttl"].as<std::string>()));
        }
        if (node["refresh_token_ttl"]) {
            auth_config.set_refresh_token_ttl(parse_duration(node["refresh_token_ttl"].as<std::string>()));
        }
    }

    // OAuth2
    if (auth_config.type() == domain::security::AuthType::OAuth2) {
        if (node["issuer_url"]) {
            auth_config.set_oauth2_issuer_url(node["issuer_url"].as<std::string>());
        }
        if (node["jwks_url"]) {
            auth_config.set_oauth2_jwks_url(node["jwks_url"].as<std::string>());
        }
    }

    return auth_config;
}

domain::security::CorsConfig YamlSchemaParser::parse_cors_node(const YAML::Node &node) const
{
    using namespace sea::domain::security;

    CorsConfig cors;

    // allowed_origins
    if (const YAML::Node origins = node["allowed_origins"]) {
        if (!origins.IsSequence()) {
            throw std::runtime_error(
                "Le champ 'cors.allowed_origins' doit être une liste dans service "
                );
        }
        std::vector<std::string> list;
        for (const auto& o : origins) {
            list.push_back(o.as<std::string>());
        }
        // Adapte selon ton API exacte de CorsConfig
        cors.set_allowed_origins(std::move(list));
    }

    // allowed_methods
    if (const YAML::Node methods = node["allowed_methods"]) {
        if (!methods.IsSequence()) {
            throw std::runtime_error(
                "Le champ 'cors.allowed_methods' doit être une liste dans service "
                );
        }
        std::vector<sea::domain::http::HttpMethod> list;
        for (const auto& m : methods) {
            try {
                list.push_back(sea::domain::http::from_string(m.as<std::string>()));
            } catch (const std::exception& e) {
                throw std::runtime_error(
                    std::string("Méthode HTTP invalide dans 'cors.allowed_methods' du service : ") + e.what()
                    );
            }
        }
        // cors.set_allowed_methods(std::move(list));
    }

    // allow_credentials
    if (node["allow_credentials"]) {
        // cors.set_allow_credentials(node["allow_credentials"].as<bool>());
    }

    // max_age
    if (node["max_age"]) {
        // cors.set_max_age(parse_duration(node["max_age"].as<std::string>()));
    }

    return cors;
}

domain::security::RateLimitRule YamlSchemaParser::parse_rate_limite_rule_node(const YAML::Node &node) const
{
    using namespace sea::domain::security;

    if (!node || !node.IsMap()) {
        throw std::runtime_error(
            "Une règle 'rate_limits' doit être un objet"
            );
    }

    const std::string scope_str = require_string(node, "scope", "rate_limit_rule");
    RateLimitScope scope;
    try {
        scope = scope_from_string(scope_str);
    } catch (const std::exception& e) {
        throw std::runtime_error(
            std::string("Scope invalide pour 'rate_limits' : ") + e.what()
            );
    }

    if (!node["requests"]) {
        throw std::runtime_error(
            "Le champ 'requests' est obligatoire dans 'rate_limits'"
            );
    }
    if (!node["window"]) {
        throw std::runtime_error(
            "Le champ 'window' est obligatoire dans 'rate_limits'"
            );
    }

    const std::uint32_t requests = node["requests"].as<std::uint32_t>();
    const std::chrono::seconds window = parse_duration(node["window"].as<std::string>());

    // burst optionnel : par défaut 2x requests
    std::uint32_t burst = requests * 2;
    if (node["burst"]) {
        burst = node["burst"].as<std::uint32_t>();
    }

    RateLimitRule rule(scope, requests, window, burst);

    try {
        rule.validate();
    } catch (const std::exception& e) {
        throw std::runtime_error(
            std::string("Règle 'rate_limits' invalide ") + e.what()
            );
    }

    return rule;
}

domain::security::HttpLimits YamlSchemaParser::parse_http_limits_node(const YAML::Node &node) const
{
    using namespace sea::domain::security;

    HttpLimits limits = HttpLimits::safe_defaults();

    if (node["max_body_size"]) {
        try {
            limits.set_max_body_size(parse_size(node["max_body_size"].as<std::string>()));
        } catch (const std::exception& e) {
            throw std::runtime_error(
                std::string("Valeur invalide pour 'http_limits.max_body_size' ") + e.what()
                );
        }
    }

    if (node["max_header_size"]) {
        limits.set_max_header_size(parse_size(node["max_header_size"].as<std::string>()));
    }

    if (node["max_headers_count"]) {
        limits.set_max_headers_count(node["max_headers_count"].as<std::uint32_t>());
    }

    if (node["max_url_length"]) {
        limits.set_max_url_length(parse_size(node["max_url_length"].as<std::string>()));
    }

    if (node["max_query_params"]) {
        limits.set_max_query_params(node["max_query_params"].as<std::uint32_t>());
    }

    if (node["request_timeout"]) {
        limits.set_request_timeout(parse_duration(node["request_timeout"].as<std::string>()));
    }

    if (node["keep_alive_timeout"]) {
        limits.set_keep_alive_timeout(parse_duration(node["keep_alive_timeout"].as<std::string>()));
    }

    if (node["max_connections_per_ip"]) {
        limits.set_max_connections_per_ip(node["max_connections_per_ip"].as<std::uint32_t>());
    }

    return limits;
}

domain::security::SecurityHeaders YamlSchemaParser::parse_security_headers_node(const YAML::Node &node) const
{
    using namespace sea::domain::security;

    // Détermine le preset de base
    SecurityHeaders headers = SecurityHeaders::recommended();

    if (const YAML::Node preset = node["preset"]) {
        const std::string name = preset.as<std::string>();
        if (name == "recommended") {
            headers = SecurityHeaders::recommended();
        } else if (name == "strict") {
            headers = SecurityHeaders::strict();
        } else if (name == "none") {
            headers = SecurityHeaders::none();
        } else {
            throw std::runtime_error(
                "Preset de headers de sécurité inconnu Valeurs possibles: 'recommended', 'strict', 'none'."
                );
        }
    }

    // Overrides individuels
    const YAML::Node overrides = node["overrides"] ? node["overrides"] : node;

    if (overrides["hsts"]) {
        headers.set_hsts(overrides["hsts"].as<std::string>());
    }
    if (overrides["x_content_type_options"]) {
        headers.set_content_type_options(overrides["x_content_type_options"].as<std::string>());
    }
    if (overrides["x_frame_options"]) {
        headers.set_frame_options(overrides["x_frame_options"].as<std::string>());
    }
    if (overrides["referrer_policy"]) {
        headers.set_referrer_policy(overrides["referrer_policy"].as<std::string>());
    }
    if (overrides["content_security_policy"]) {
        headers.set_content_security_policy(overrides["content_security_policy"].as<std::string>());
    }
    if (overrides["permissions_policy"]) {
        headers.set_permissions_policy(overrides["permissions_policy"].as<std::string>());
    }

    return headers;
}
sea::domain::DatabaseConfig
YamlSchemaParser::parse_database_config_node(const YAML::Node& node) const {
    sea::domain::DatabaseConfig config{};

    const std::string type_str = get_or_default<std::string>(node, "type", "memory");
    config.type = parse_database_type(type_str);

    config.host          = get_or_default<std::string>(node, "host", config.host);
    config.port          = get_or_default<int>(node, "port", config.port);
    config.database_name = get_or_default<std::string>(node, "database_name", "");
    config.username      = get_or_default<std::string>(node, "username", "");
    config.password      = get_or_default<std::string>(node, "password", "");

    return config;
}

sea::domain::RelationKind
YamlSchemaParser::parse_relation_kind(const std::string& value) const {
    const std::string lowered = to_lower(value);

    if (lowered == "belongs_to" || lowered == "belongsto") {
        return sea::domain::RelationKind::BelongsTo;
    }
    if (lowered == "has_many" || lowered == "hasmany") {
        return sea::domain::RelationKind::HasMany;
    }
    if (lowered == "has_one" || lowered == "hasone") {
        return sea::domain::RelationKind::HasOne;
    }
    if (lowered == "many_to_many" || lowered == "manytomany") {
        return sea::domain::RelationKind::ManyToMany;
    }

    throw std::runtime_error("Type de relation inconnu: '" + value + "'.");
}

sea::domain::OnDelete
YamlSchemaParser::parse_on_delete(const std::string& value) const {
    const std::string lowered = to_lower(value);

    if (lowered == "cascade") {
        return sea::domain::OnDelete::Cascade;
    }
    if (lowered == "set_null" || lowered == "setnull") {
        return sea::domain::OnDelete::SetNull;
    }
    if (lowered == "restrict") {
        return sea::domain::OnDelete::Restrict;
    }

    throw std::runtime_error("Valeur on_delete inconnue: '" + value + "'.");
}

sea::domain::DatabaseType
YamlSchemaParser::parse_database_type(const std::string& value) const {
    const std::string lowered = to_lower(value);

    if (lowered == "memory") {
        return sea::domain::DatabaseType::Memory;
    }
    if (lowered == "mysql" || lowered == "mysqldb") {
        return sea::domain::DatabaseType::MySQL;
    }
    if (lowered == "postgres" || lowered == "postgresql") {
        return sea::domain::DatabaseType::PostgreSQL;
    }
    if (lowered == "mongo" || lowered == "mongodb") {
        return sea::domain::DatabaseType::MongoDB;
    }

    throw std::runtime_error("Type de base de donnees inconnu: '" + value + "'.");
}
// =====================================================================
//                    HELPERS DE PARSING
// =====================================================================

std::chrono::seconds
YamlSchemaParser::parse_duration(
    const std::string& s) const
{
    if (s.empty()) {
        throw std::runtime_error("Durée vide");
    }

    std::size_t suffix_pos = 0;
    while (suffix_pos < s.size() && std::isdigit(static_cast<unsigned char>(s[suffix_pos]))) {
        ++suffix_pos;
    }

    if (suffix_pos == 0) {
        throw std::runtime_error("Durée invalide (pas de nombre): '" + s + "'");
    }

    const std::uint64_t number = std::stoull(s.substr(0, suffix_pos));
    const std::string suffix = s.substr(suffix_pos);

    using namespace std::chrono;
    if (suffix.empty() || suffix == "s") return seconds(number);
    if (suffix == "m") return seconds(number * 60);
    if (suffix == "h") return seconds(number * 3600);
    if (suffix == "d") return seconds(number * 86400);

    throw std::runtime_error("Suffixe de durée inconnu: '" + suffix + "' dans '" + s + "'");
}

std::uint64_t
YamlSchemaParser::parse_size(
    const std::string& s) const
{
    if (s.empty()) {
        throw std::runtime_error("Taille vide");
    }

    std::size_t suffix_pos = 0;
    while (suffix_pos < s.size() &&
           (std::isdigit(static_cast<unsigned char>(s[suffix_pos])) || s[suffix_pos] == '.')) {
        ++suffix_pos;
    }

    if (suffix_pos == 0) {
        throw std::runtime_error("Taille invalide (pas de nombre): '" + s + "'");
    }

    const std::uint64_t number = std::stoull(s.substr(0, suffix_pos));
    const std::string suffix = s.substr(suffix_pos);

    if (suffix.empty() || suffix == "B") return number;
    if (suffix == "KB" || suffix == "K") return number * 1024;
    if (suffix == "MB" || suffix == "M") return number * 1024 * 1024;
    if (suffix == "GB" || suffix == "G") return number * 1024ULL * 1024 * 1024;

    throw std::runtime_error("Suffixe de taille inconnu: '" + suffix + "' dans '" + s + "'");
}

} // namespace sea::infrastructure::yaml