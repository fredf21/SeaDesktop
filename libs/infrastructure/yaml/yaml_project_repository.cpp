#include "yaml_project_repository.h"



sea::domain::Project sea::infrastructure::yaml::yaml_project_repository::load(const std::filesystem::path &source) const
{
    if (!fs::exists(source)) {
        throw std::runtime_error("Fichier YAML introuvable : " + source.string());
    }

    YAML::Node root = YAML::LoadFile(source.string());

    if (!root || !root.IsMap()) {
        throw std::runtime_error("Le fichier YAML racine doit être un objet.");
    }

    sea::domain::Project project{};

    // Supporte les 2 styles :
    // 1) root["project"]["name"]
    // 2) root["name"]
    if (root["project"]) {
        if (!root["project"].IsMap()) {
            throw std::runtime_error("Le champ 'project' doit être un objet.");
        }

        project.name = require_string(root["project"], "name", "project");
    } else if (root["name"]) {
        project.name = root["name"].as<std::string>();
    }

    const YAML::Node services_node = root["services"];
    if (services_node) {
        if (!services_node.IsSequence()) {
            throw std::runtime_error("Le champ 'services' doit être une liste.");
        }

        for (const auto& service_node : services_node) {
            project.services.push_back(parse_service_node(service_node));
        }
    }

    return project;
}

sea::domain::Service sea::infrastructure::yaml::yaml_project_repository::parse_service_node(const YAML::Node &node) const
{
    if (!node || !node.IsMap()) {
        throw std::runtime_error("Un service YAML doit être un objet.");
    }

    sea::domain::Service service{};

    service.name = require_string(node, "name", "service");

    // Port
    try {
        service.port = get_or_default<std::uint16_t>(node, "port", service.port);
    } catch (const std::exception& e) {
        throw std::runtime_error(
            "Valeur invalide pour 'port' dans service '" + service.name + "': " + e.what()
            );
    }

    // Database config
    const YAML::Node database_node = node["database"];
    if (database_node) {
        if (!database_node.IsMap()) {
            throw std::runtime_error(
                "Le champ 'database' doit être un objet dans service '" + service.name + "'."
                );
        }

        const std::string db_type =
            get_or_default<std::string>(database_node, "type", "memory");

        if (db_type == "memory") {
            service.database_config.type = sea::domain::DatabaseType::Memory;
        } else if (db_type == "postgres" || db_type == "postgresql") {
            service.database_config.type = sea::domain::DatabaseType::PostgreSQL;
        } else if (db_type == "mongo" || db_type == "mongodb") {
            service.database_config.type = sea::domain::DatabaseType::MongoDB;
        } else {
            throw std::runtime_error(
                "Type de base de donnees inconnu dans service '" + service.name +
                "': '" + db_type + "'."
                );
        }

        service.database_config.host =
            get_or_default<std::string>(database_node, "host", service.database_config.host);

        service.database_config.port =
            get_or_default<int>(database_node, "port", service.database_config.port);

        service.database_config.database_name =
            get_or_default<std::string>(database_node, "database_name", service.database_config.database_name);

        service.database_config.username =
            get_or_default<std::string>(database_node, "username", service.database_config.username);

        service.database_config.password =
            get_or_default<std::string>(database_node, "password", service.database_config.password);
    }

    // Service options
    const YAML::Node options_node = node["options"];
    if (options_node) {
        if (!options_node.IsMap()) {
            throw std::runtime_error(
                "Le champ 'options' doit être un objet dans service '" + service.name + "'."
                );
        }

        service.options.enable_logs =
            get_or_default<bool>(options_node, "enable_logs", service.options.enable_logs);

        service.options.enable_metrics =
            get_or_default<bool>(options_node, "enable_metrics", service.options.enable_metrics);

        service.options.enable_swagger =
            get_or_default<bool>(options_node, "enable_swagger", service.options.enable_swagger);

        service.options.enable_healthcheck =
            get_or_default<bool>(options_node, "enable_healthcheck", service.options.enable_healthcheck);
    }

    // Entities
    const YAML::Node entities_node = node["entities"];
    if (entities_node) {
        if (!entities_node.IsSequence()) {
            throw std::runtime_error(
                "Le champ 'entities' doit être une liste dans service '" + service.name + "'."
                );
        }

        for (const auto& entity_node : entities_node) {
            service.schema.entities.push_back(parse_entity_node(entity_node));
        }
    }

    return service;
}

sea::domain::Entity sea::infrastructure::yaml::yaml_project_repository::parse_entity_node(const YAML::Node &node) const
{
    if (!node || !node.IsMap()) {
        throw std::runtime_error("Une entité YAML doit être un objet.");
    }

    sea::domain::Entity entity{};

    entity.name = require_string(node, "name", "entity");
    entity.table_name = get_or_default<std::string>(node, "table_name", "");

    const YAML::Node fields_node = node["fields"];
    if (!fields_node || !fields_node.IsSequence()) {
        throw std::runtime_error(
            "Le champ 'fields' est obligatoire et doit être une liste dans entity '"
            + entity.name + "'."
            );
    }

    for (const auto& field_node : fields_node) {
        entity.fields.push_back(parse_field_node(field_node));
    }

    const YAML::Node relations_node = node["relations"];
    if (relations_node) {
        if (!relations_node.IsSequence()) {
            throw std::runtime_error(
                "Le champ 'relations' doit être une liste dans entity '"
                + entity.name + "'."
                );
        }

        for (const auto& relation_node : relations_node) {
            entity.relations.push_back(parse_relation_node(relation_node));
        }
    }

    const YAML::Node options_node = node["options"];
    if (options_node) {
        if (!options_node.IsMap()) {
            throw std::runtime_error(
                "Le champ 'options' doit être un objet dans entity '"
                + entity.name + "'."
                );
        }

        entity.options.enable_crud =
            get_or_default<bool>(options_node, "enable_crud", true);
        entity.options.enable_auth =
            get_or_default<bool>(options_node, "enable_auth", false);
        entity.options.enable_websocket =
            get_or_default<bool>(options_node, "enable_websocket", false);
        entity.options.soft_delete =
            get_or_default<bool>(options_node, "soft_delete", false);
        entity.options.timestamps =
            get_or_default<bool>(options_node, "timestamps", true);
    }

    return entity;
}

sea::domain::Field sea::infrastructure::yaml::yaml_project_repository::parse_field_node(const YAML::Node &node) const
{
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

    field.required = get_or_default<bool>(node, "required", field.required);
    field.unique = get_or_default<bool>(node, "unique", field.unique);
    field.indexed = get_or_default<bool>(node, "indexed", field.indexed);
    field.serializable = get_or_default<bool>(node, "serializable", field.serializable);

    if (node["max_length"]) {
        try {
            field.max_length = node["max_length"].as<std::size_t>();
        } catch (const YAML::Exception& e) {
            throw std::runtime_error(
                "Valeur invalide pour 'max_length' dans field '" + field.name + "': " + e.what()
                );
        }
    }

    if (node["min_value"]) {
        try {
            if (node["min_value"].IsScalar()) {
                const std::string raw = node["min_value"].Scalar();

                if (raw.find('.') != std::string::npos) {
                    field.min_value = node["min_value"].as<double>();
                } else {
                    field.min_value = node["min_value"].as<int64_t>();
                }
            } else {
                throw std::runtime_error("Le champ 'min_value' doit être scalaire.");
            }
        } catch (const YAML::Exception& e) {
            throw std::runtime_error(
                "Valeur invalide pour 'min_value' dans field '" + field.name + "': " + e.what()
                );
        }
    }

    if (node["max_value"]) {
        try {
            if (node["max_value"].IsScalar()) {
                const std::string raw = node["max_value"].Scalar();

                if (raw.find('.') != std::string::npos) {
                    field.max_value = node["max_value"].as<double>();
                } else {
                    field.max_value = node["max_value"].as<int64_t>();
                }
            } else {
                throw std::runtime_error("Le champ 'max_value' doit être scalaire.");
            }
        } catch (const YAML::Exception& e) {
            throw std::runtime_error(
                "Valeur invalide pour 'max_value' dans field '" + field.name + "': " + e.what()
                );
        }
    }

    if (node["default"]) {
        try {
            if (!node["default"].IsScalar()) {
                throw std::runtime_error(
                    "Le champ 'default' dans field '" + field.name + "' doit être scalaire."
                    );
            }

            const std::string raw = node["default"].Scalar();

            if (raw == "true" || raw == "false") {
                field.default_val = node["default"].as<bool>();
            } else if (raw.find('.') != std::string::npos) {
                field.default_val = node["default"].as<double>();
            } else {
                try {
                    field.default_val = node["default"].as<int64_t>();
                } catch (const YAML::Exception&) {
                    field.default_val = raw;
                }
            }
        } catch (const YAML::Exception& e) {
            throw std::runtime_error(
                "Valeur invalide pour 'default' dans field '" + field.name + "': " + e.what()
                );
        }
    }

    return field;
}
sea::domain::Relation sea::infrastructure::yaml::yaml_project_repository::parse_relation_node(const YAML::Node &node) const
{
    if (!node || !node.IsMap()) {
        throw std::runtime_error("Une relation YAML doit être un objet.");
    }

    sea::domain::Relation relation{};

    relation.name = require_string(node, "name", "relation");
    relation.target_entity = require_string(node, "target_entity", "relation");

    const std::string kind_str = require_string(node, "kind", "relation");
    relation.kind = parse_relation_kind(kind_str);

    const std::string on_delete_str =
        get_or_default<std::string>(node, "on_delete", "restrict");
    relation.on_delete = parse_on_delete(on_delete_str);

    relation.fk_column = get_or_default<std::string>(node, "fk_column", "");
    relation.pivot_table = get_or_default<std::string>(node, "pivot_table", "");

    if (relation.uses_pivot_table() && relation.pivot_table.empty()) {
        throw std::runtime_error(
            "La relation '" + relation.name +
            "' de type many_to_many doit définir 'pivot_table'."
            );
    }

    if (!relation.uses_pivot_table() && relation.uses_local_foreign_key() &&
        relation.fk_column.empty()) {
        throw std::runtime_error(
            "La relation '" + relation.name +
            "' de type belongs_to doit définir 'fk_column'."
            );
    }

    return relation;
}
sea::domain::RelationKind sea::infrastructure::yaml::yaml_project_repository::parse_relation_kind(const std::string &value) const
{
    if (value == "belongs_to") {
        return sea::domain::RelationKind::BelongsTo;
    }
    if (value == "has_many") {
        return sea::domain::RelationKind::HasMany;
    }
    if (value == "has_one") {
        return sea::domain::RelationKind::HasOne;
    }
    if (value == "many_to_many") {
        return sea::domain::RelationKind::ManyToMany;
    }

    throw std::runtime_error("Type de relation inconnu: '" + value + "'.");
}

sea::domain::OnDelete sea::infrastructure::yaml::yaml_project_repository::parse_on_delete(const std::string &value) const
{
    if (value == "cascade") {
        return sea::domain::OnDelete::Cascade;
    }
    if (value == "set_null") {
        return sea::domain::OnDelete::SetNull;
    }
    if (value == "restrict") {
        return sea::domain::OnDelete::Restrict;
    }

    throw std::runtime_error("Valeur on_delete inconnue: '" + value + "'.");
}

std::string sea::infrastructure::yaml::yaml_project_repository::require_string(const YAML::Node &node, const char *key, const char *context) const
{
    if (!node || !node[key]) {
        throw std::runtime_error(
            std::string("Le champ '") + key + "' est obligatoire dans " + context + "."
            );
    }

    try {
        return node[key].as<std::string>();
    } catch (const YAML::Exception& e) {
        throw std::runtime_error(
            std::string("Le champ '") + key + "' dans " + context +
            " doit être une chaîne valide: " + e.what()
            );
    }
}
