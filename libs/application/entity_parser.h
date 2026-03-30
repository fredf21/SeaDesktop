#pragma once

#include "domain/entity.h"
#include "domain/field_type.h"

#include <yaml-cpp/yaml.h>
#include <stdexcept>
#include <string>
#include <filesystem>

namespace application {

class EntityParser {
public:
    // Charge un fichier YAML et retourne une Entity
    // Lance std::runtime_error si le fichier est invalide
    static domain::Entity from_file(const std::string& path) {
        YAML::Node root;
        try {
            root = YAML::LoadFile(path);
        } catch (const YAML::Exception& e) {
            throw std::runtime_error("EntityParser: cannot load '" + path + "': " + e.what());
        }
        return parse(root);
    }

    // Parse directement depuis une string YAML (utile pour les tests)
    static domain::Entity from_string(const std::string& yaml) {
        YAML::Node root = YAML::Load(yaml);
        return parse(root);
    }
    // parser un directoire
    static std::vector<domain::Entity> from_directory(const std::string& directory){
        std::vector<domain::Entity> entities;

        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            const auto ext = entry.path().extension().string();
            if (ext == ".yaml" || ext == ".yml") {
                entities.push_back(from_file(entry.path().string()));
            }
        }

        return entities;
    }

private:
    static domain::Entity parse(const YAML::Node& root) {
        domain::Entity entity;

        // ── name ─────────────────────────────────────────────────
        if (!root["entity"])
            throw std::runtime_error("EntityParser: missing 'entity' key");
        entity.name = root["entity"].as<std::string>();

        // table_name: optionnel, sinon calculé
        if (root["table"])
            entity.table_name = root["table"].as<std::string>();

        // ── options ───────────────────────────────────────────────
        if (root["auth"])
            entity.options.enable_auth = root["auth"].as<bool>();
        if (root["websocket"])
            entity.options.enable_websocket = root["websocket"].as<bool>();
        if (root["soft_delete"])
            entity.options.soft_delete = root["soft_delete"].as<bool>();
        if (root["timestamps"])
            entity.options.timestamps = root["timestamps"].as<bool>();

        // ── fields ────────────────────────────────────────────────
        if (!root["fields"] || !root["fields"].IsSequence())
            throw std::runtime_error("EntityParser: 'fields' must be a sequence");

        for (const auto& node : root["fields"])
            entity.fields.push_back(parse_field(node));

        // ── relations ─────────────────────────────────────────────
        if (root["relations"] && root["relations"].IsSequence())
            for (const auto& node : root["relations"])
                entity.relations.push_back(parse_relation(node));

        return entity;
    }

    static domain::Field parse_field(const YAML::Node& node) {
        if (!node["name"])
            throw std::runtime_error("EntityParser: field missing 'name'");
        if (!node["type"])
            throw std::runtime_error("EntityParser: field missing 'type'");

        std::string type_str = node["type"].as<std::string>();
        auto type_opt = domain::field_type_from_string(type_str);
        if (!type_opt)
            throw std::runtime_error("EntityParser: unknown field type '" + type_str + "'");

        domain::Field f = domain::make_field(node["name"].as<std::string>(), *type_opt);

        // Password jamais sérialisé
        if (f.type == domain::FieldType::Password)
            f.serializable = false;

        if (node["required"])  f.required  = node["required"].as<bool>();
        if (node["unique"])    f.unique    = node["unique"].as<bool>();
        if (node["indexed"])   f.indexed   = node["indexed"].as<bool>();
        if (node["max_length"])f.max_length= node["max_length"].as<size_t>();

        return f;
    }

    static domain::Relation parse_relation(const YAML::Node& node) {
        domain::Relation r;
        if (!node["name"])   throw std::runtime_error("EntityParser: relation missing 'name'");
        if (!node["target"]) throw std::runtime_error("EntityParser: relation missing 'target'");
        if (!node["kind"])   throw std::runtime_error("EntityParser: relation missing 'kind'");

        r.name          = node["name"].as<std::string>();
        r.target_entity = node["target"].as<std::string>();

        std::string kind = node["kind"].as<std::string>();
        if      (kind == "belongs_to")  r.kind = domain::RelationKind::BelongsTo;
        else if (kind == "has_many")    r.kind = domain::RelationKind::HasMany;
        else if (kind == "has_one")     r.kind = domain::RelationKind::HasOne;
        else if (kind == "many_to_many")r.kind = domain::RelationKind::ManyToMany;
        else throw std::runtime_error("EntityParser: unknown relation kind '" + kind + "'");

        if (node["on_delete"]) {
            std::string od = node["on_delete"].as<std::string>();
            if      (od == "cascade")  r.on_delete = domain::OnDelete::Cascade;
            else if (od == "set_null") r.on_delete = domain::OnDelete::SetNull;
            else                       r.on_delete = domain::OnDelete::Restrict;
        }

        return r;
    }
};

} // namespace application
