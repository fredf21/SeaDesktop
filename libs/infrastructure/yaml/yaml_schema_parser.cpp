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
        entity.options.enable_auth =
            get_or_default<bool>(options_node, "enable_auth", entity.options.enable_auth);
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

} // namespace sea::infrastructure::yaml