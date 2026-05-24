#include "yaml_schema_parser.h"
#include "exception_handling.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>

namespace YAML {
template<>
struct convert<sea::domain::MigrationMode> {
    static bool decode(const Node& node, sea::domain::MigrationMode& mode) {
        if (!node.IsScalar()) return false;

        std::string value = node.as<std::string>();

        if (value == "conservative") {
            mode = sea::domain::MigrationMode::Conservative;
            return true;
        }
        if (value == "modified") {
            mode = sea::domain::MigrationMode::Modified;
            return true;
        }

        if (value == "aggressive") {
            mode = sea::domain::MigrationMode::Aggressive;
            return true;
        }

        return false;
    }
};
}
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
        throw sea::sea_errors_handling::YamlParsingException(
            std::string("[YAML PARSING EXCEPTION] Invalid value for '") + key + "': " + e.what()
            );
    }
}
// Lit une liste de strings depuis un nœud YAML scalaire ou séquence
[[nodiscard]] std::vector<std::string>
parse_string_list(const YAML::Node& node, const char* key, const std::string& context) {
    std::vector<std::string> out;
    out.clear();
    if (!node || !node[key]) {
        return out;
    }

    const YAML::Node value = node[key];

    if (value.IsSequence()) {
        for (const auto& item : value) {
            try {
                out.push_back(item.as<std::string>());
            } catch (const YAML::Exception& e) {
                throw sea::sea_errors_handling::YamlParsingException(
                    std::string("[YAML PARSING EXCEPTION] Invalid Element in '") + key + "' (" + context + "): " + e.what()
                    );
            }
        }
    } else if (value.IsScalar()) {
        // Tolère une seule valeur sous forme scalaire
        try {
            out.push_back(value.as<std::string>());
        } catch (const YAML::Exception& e) {
            throw sea::sea_errors_handling::YamlParsingException(
                std::string("[YAML PARSING EXCEPTION] Invalid value for '") + key + "' (" + context + "): " + e.what()
                );
        }
    } else {
        throw sea::sea_errors_handling::YamlParsingException(
            std::string("[YAML PARSING EXCEPTION] '") + key + "' must be a list in " + context + "."
            );
    }

    return out;
}


} // namespace

bool YamlSchemaParser::has_key(const YAML::Node& node, const char* key) const {
    return node && node[key];
}

std::string YamlSchemaParser::require_string(const YAML::Node& node,
                                             const char* key,
                                             const char* context) const {
    if (!has_key(node, key)) {
        throw sea::sea_errors_handling::YamlParsingException(
            std::string("[YAML PARSING EXCEPTION] Missing mandatory field '") + key + "' dans " + context
            );
    }

    try {
        return node[key].as<std::string>();
    } catch (const YAML::Exception& e) {
        throw sea::sea_errors_handling::YamlParsingException(
            std::string("[YAML PARSING EXCEPTION] field '") + key + "' invalid in " + context + ": " + e.what()
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
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] Missing environment variable: " + var_name
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
        throw sea::sea_errors_handling::YamlParsingException(
            "[YAML PARSING EXCEPTION] Can't load Yaml file '" + file_path + "': " + e.what()
            );
    }

    return parse_project_node(root);
}

sea::domain::Service YamlSchemaParser::parse_service_file(const std::string& file_path) const {
    YAML::Node root;

    try {
        root = YAML::LoadFile(file_path);
    } catch (const YAML::Exception& e) {
        throw sea::sea_errors_handling::YamlParsingException(
            "[YAML PARSING EXCEPTION]  Can't load Yaml file '" + file_path + "': " + e.what()
            );
    }

    return parse_service_node(root);
}

sea::domain::Project YamlSchemaParser::parse_project_node(const YAML::Node& root) const {
    if (!root || !root.IsMap()) {
        throw sea::sea_errors_handling::YamlParsingException("[YAML PARSING EXCEPTION] The root YAML document must be an object.");
    }

    sea::domain::Project project{};

    // project:
    //   name: ...
    if (has_key(root, "project")) {
        const YAML::Node project_node = root["project"];

        if (!project_node.IsMap()) {
            throw sea::sea_errors_handling::YamlParsingException("[YAML PARSING EXCEPTION] 'project' must be an object.");
        }

        project.name = require_string(project_node, "name", "project");
    } else {
        project.name = "UnnamedProject";
    }

    // services:
    if (!has_key(root, "services")) {
        throw sea::sea_errors_handling::YamlParsingException("[YAML PARSING EXCEPTION] The 'services' field is required at the root.");
    }

    const YAML::Node services_node = root["services"];
    if (!services_node.IsSequence()) {
        throw sea::sea_errors_handling::YamlParsingException("[YAML PARSING EXCEPTION] 'services' must be a list.");
    }

    for (const auto& service_node : services_node) {
        project.services.push_back(parse_service_node(service_node));
    }

    return project;
}

sea::domain::Service YamlSchemaParser::parse_service_node(const YAML::Node& node) const {
    if (!node || !node.IsMap()) {
        throw sea::sea_errors_handling::YamlParsingException("[YAML PARSING EXCEPTION] A YAML service an object.");
    }

    sea::domain::Service service{};

    service.name = require_string(node, "name", "service");

    const int raw_port = get_or_default<int>(node, "port", 8080);
    if (raw_port < 1 || raw_port > 65535) {
        throw sea::sea_errors_handling::YamlParsingException(
            "[YAML PARSING EXCEPTION] Le port du service '" + service.name + "' doit être entre 1 et 65535."
            );
    }
    service.port = static_cast<std::uint16_t>(raw_port);

    // database:
    if (has_key(node, "database")) {
        const YAML::Node db_node = node["database"];
        if (!db_node.IsMap()) {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] 'database' field must be an object in service '" + service.name + "'."
                );
        }

        service.database_config = parse_database_config_node(db_node);
    }

    // ── Section storage (optionnelle) ────────────────────────
    // Si absente : service.storage reste std::nullopt. Le
    // FileServiceFactory applique un fallback automatique si le
    // schema a au moins un champ File (cf. has_file_fields()).
    if (has_key(node, "storage")) {
        const YAML::Node storage_node = node["storage"];
        if (!storage_node.IsMap()) {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] 'storage' field must be an object in service '" +
                service.name + "'.");
        }
        service.storage = parse_storage_config_node(storage_node);
    }

    // Si Pas de section security : defaults disable
    service.security = sea::domain::security::SecurityConfig::disabled();

    if(has_key(node, "security")){
        const YAML::Node security_node = node["security"];
        if(!security_node.IsMap()){
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] 'security' field must be an object in service  '" + service.name + "'."
                );
        }
        service.security = parse_security_node(security_node);
    }

    if (has_key(node, "security") && has_key(node["security"], "authorization")) {
        service.access_control = parse_authorization_node(
            node["security"]["authorization"]
            );
    } else {
        service.access_control = sea::domain::access_control::AccessControlConfig::disabled();
    }

    //  Section logging
    // ═══════════════════════════════════════════════════════════════════
    // Exemple complet de YAML qu'on peut maintenant parser :
    //
    // services:
    //   - name: CCNBService
    //     port: 8081
    //
    //     logging:
    //       level: info
    //       modules:
    //         sea.http: debug
    //         sea.persistence: info
    //         seastar: warn
    //       sinks:
    //         - type: console
    //           format: text
    //           enabled: true
    //         - type: file
    //           format: json
    //           enabled: true
    //           path: "./logs/ccnb.log"
    //           rotation:
    //             max_size: "100MB"
    //             time_pattern: daily
    //             max_files: 10
    //             compress: false
    //       flush_level: error
    //       async:
    //         enabled: true
    //         queue_size: 8192
    //         overflow_policy: overrun_oldest
    // ═══════════════════════════════════════════════════════════════════

    if (has_key(node, "logging")) {
        service.logging = parse_logging_node(node["logging"]);
    } else {
        // Pas de section logging : defauts (console texte info)
        service.logging = sea::domain::logging::LoggingConfig::safe_defaults();
    }

    // entities:
    if (has_key(node, "entities")) {
        const YAML::Node entities_node = node["entities"];

        if (!entities_node.IsSequence()) {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] 'entities' field must be a list in '" + service.name + "' service."
                );
        }

        for (const auto& entity_node : entities_node) {
            service.schema.entities.push_back(
                parse_entity_node(entity_node, service.access_control)
                );
        }
    }

    return service;
}

sea::domain::Entity YamlSchemaParser::parse_entity_node(const YAML::Node& node, const sea::domain::access_control::AccessControlConfig& global_config) const {
    if (!node || !node.IsMap()) {
        throw sea::sea_errors_handling::YamlParsingException("[YAML PARSING EXCEPTION] A YAML entity must be an object.");
    }

    sea::domain::Entity entity{};

    entity.name = require_string(node, "name", "entity");
    std::string s = domain::Entity::to_route_plural(entity.name);
    entity.table_name = get_or_default<std::string>(node, "table_name", s);
    // options:
    if (has_key(node, "options")) {
        const YAML::Node options_node = node["options"];
        if (!options_node.IsMap()) {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] 'options' field must be an object in '" + entity.name + "' entity."
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
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] 'fields' must be a list in '" + entity.name + "' entity."
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
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] 'relations' must be a list in '" + entity.name + "' entity."
                );
        }

        for (const auto& relation_node : relations_node) {
            entity.relations.push_back(parse_relation_node(relation_node));
        }
    }
    // parser le bloc 'seeds:' de l'entite
    if (has_key(node, "seeds")) {
        const YAML::Node seeds_node = node["seeds"];
        if (!seeds_node.IsSequence()) {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] 'seeds' must be a list in '" + entity.name + "' entity."
                );
        }
        for (const auto& seed_node : seeds_node) {
            entity.seeds.push_back(parse_seed_record_node(seed_node, entity));
        }
    }
    // pagination: (bloc optionnel)
    if (has_key(node, "pagination")) {
        const YAML::Node pagination_node = node["pagination"];
        if (!pagination_node.IsMap()) {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] 'pagination' must be a list in '" + entity.name + "' entity."
                );
        }

        entity.pagination = parse_pagination_node(pagination_node, entity.name);
    }

    // access_control pour cette entité
    entity.access_control = parse_entity_access_control_node(node, entity, global_config);

    return entity;
}

sea::domain::Field YamlSchemaParser::parse_field_node(const YAML::Node& node) const {
    if (!node || !node.IsMap()) {
        throw sea::sea_errors_handling::YamlParsingException("[YAML PARSING EXCEPTION] A YAML field must be an object.");
    }

    sea::domain::Field field{};

    field.name = require_string(node, "name", "field");
    // previous_name pour rename explicite
    if (auto prev_name_node = node["previous_name"]; prev_name_node) {
        const auto prev_name = prev_name_node.as<std::string>("");
        if (!prev_name.empty()) {
            field.previous_name = prev_name;
        }
    }

    const std::string type_str = require_string(node, "type", "field");
    const auto field_type = sea::domain::field_type_from_string(type_str);
    if (!field_type.has_value()) {
        throw sea::sea_errors_handling::YamlParsingException("[YAML PARSING EXCEPTION] Unknown field type: '" + type_str + "'.");
    }
    field.type = *field_type;

    if (field.type != domain::FieldType::Native && has_key(node, "native")) {
        throw sea::sea_errors_handling::YamlParsingException(
            "[YAML PARSING EXCEPTION] The native node is allowed only with type=native."
            );
    }

    if (field.type == domain::FieldType::Native) {
        if(!has_key(node, "native"))
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] The native node is allowed only with type=native."
                );
        const auto native_node = node["native"];


        if (!has_key(native_node, "dialect")) {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] Le noeud native du champ '" + field.name +
                "' must contain 'dialect'"
                );
        }
        if (!has_key(native_node, "type")) {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] The native node of the field '" + field.name +
                "' must contain 'type'"
                );
        }
        sea::domain::NativeDbType native_type;

        native_type.dialect =
            parse_database_dialect_node(native_node["dialect"].as<std::string>());

        native_type.type_name =
            native_node["type"].as<std::string>();

        field.native_type = native_type;
    }

    field.required          = get_or_default<bool>(node, "required", field.required);
    field.unique            = get_or_default<bool>(node, "unique", field.unique);
    field.indexed           = get_or_default<bool>(node, "indexed", field.indexed);
    field.serializable      = get_or_default<bool>(node, "serializable", field.serializable);
    field.unsigned_value    = get_or_default<bool>(node, "unsigned_value", field.unsigned_value);

    // Contrainte utile : password non sérialisable par défaut si rien n'est précisé
    if (field.type == sea::domain::FieldType::Password && !has_key(node, "serializable")) {
        field.serializable = false;
    }

    // max_length
    if (has_key(node, "max_length")) {
        try {
            field.max_length = node["max_length"].as<std::size_t>();
        } catch (const YAML::Exception& e) {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] max_length invalid for the field '" + field.name + "': " + e.what()
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
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] min_value invalid for the field '" + field.name + "': " + e.what()
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
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] max_value invalid for the field '" + field.name + "': " + e.what()
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
            case sea::domain::FieldType::Decimal:
            case sea::domain::FieldType::Json:
            case sea::domain::FieldType::Native:
                field.default_val = node["default"].as<std::string>();
                break;

            case sea::domain::FieldType::Int:
            case sea::domain::FieldType::SmallInt:
            case sea::domain::FieldType::BigInt:
                field.default_val = node["default"].as<std::int64_t>();
                break;

            case sea::domain::FieldType::Float:
                field.default_val = node["default"].as<double>();
                break;

            case sea::domain::FieldType::Bool:
                field.default_val = node["default"].as<bool>();
                break;

            case sea::domain::FieldType::Binary:
                throw sea::sea_errors_handling::YamlParsingException(
                    "[YAML PARSING EXCEPTION] Le champ '" + field.name + "' is of type Binary and cannot have a default value."
                    );

            case sea::domain::FieldType::File:
                throw sea::sea_errors_handling::YamlParsingException(
                    "[YAML PARSING EXCEPTION] Le champ '" + field.name + "' is of type File and cannot have a default value; a file must be uploaded explicitly."
                    );
            }
        } catch (const YAML::Exception& e) {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] Invalid default value for field '" + field.name + "': " + e.what()
                );
        }
    }

    // ─────────────────────────────────────────────────────────────
    // Sous-bloc `file:` — OBLIGATOIRE pour les champs de type File.
    //
    // Un champ déclaré `type: file` SANS sous-bloc `file:` est rejeté
    // ici dès le parsing : sans config, on ne saurait ni où stocker,
    // ni quoi accepter, ni que faire à la suppression.
    //
    // À l'inverse, déclarer un bloc `file:` sur un type ≠ File est
    // une erreur de cohérence (silencieusement ignorée serait pire) :
    // on rejette aussi.
    // ─────────────────────────────────────────────────────────────
    const bool has_file_block = has_key(node, "file");

    if (field.type == sea::domain::FieldType::File) {
        if (!has_file_block) {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] Field '" + field.name +
                "' is of type 'file' but does not declare the 'file:' sub-block "
                "(required to configure max_size, mime, extensions, storage_path, on_delete)."
                );
        }
        field.file_config = parse_file_field_config_node(node["file"], field.name);
    } else if (has_file_block) {
        throw sea::sea_errors_handling::YamlParsingException(
            "[YAML PARSING EXCEPTION] Field '" + field.name +
            "' declares a 'file:' sub-block but is not of type 'file' "
            "(current type: '" + std::string(sea::domain::to_string(field.type)) + "')."
            );
    }

    return field;
}

// ─────────────────────────────────────────────────────────────
// parse_file_field_config_node
//
// Parse le sous-bloc `file:` d'un champ File et retourne un
// FileFieldConfig prêt à l'emploi.
//
// Toutes les clés sont OPTIONNELLES sauf `storage_path` (sans path,
// on stockerait tout à la racine, source de collisions et de fouillis).
// Les défauts appliqués correspondent à ceux de FileFieldConfig :
//   - max_size_bytes      : std::nullopt (pas de limite per-field ;
//                           la limite globale du body HTTP s'applique
//                           toujours, cf. http_limits)
//   - allowed_mime_types  : [] (tout accepté)
//   - allowed_extensions  : [] (tout accepté, points ajoutés au besoin)
//   - on_delete           : Cascade
// ─────────────────────────────────────────────────────────────
sea::domain::FileFieldConfig
YamlSchemaParser::parse_file_field_config_node(const YAML::Node& node,
                                               const std::string& field_name) const
{
    if (!node || !node.IsMap()) {
        throw sea::sea_errors_handling::YamlParsingException(
            "[YAML PARSING EXCEPTION] The 'file:' sub-block of field '" + field_name +
            "' must be an object (map)."
            );
    }

    sea::domain::FileFieldConfig cfg{};

    // ── max_size ────────────────────────────────────────────
    // Réutilise parse_size pour accepter les unités humaines :
    // "5MB", "500KB", "1GB", ou un nombre brut en bytes.
    if (has_key(node, "max_size")) {
        try {
            const auto raw = node["max_size"].as<std::string>();
            const std::uint64_t parsed = parse_size(raw);
            if (parsed == 0) {
                throw sea::sea_errors_handling::YamlParsingException(
                    "[YAML PARSING EXCEPTION] max_size of field '" + field_name +
                    "' must be > 0 (received value: '" + raw + "')."
                    );
            }
            cfg.max_size_bytes = static_cast<std::size_t>(parsed);
        } catch (const YAML::Exception& e) {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] Invalid max_size for field '" + field_name +
                "': " + e.what()
                );
        }
    }

    // ── allowed_mime_types ──────────────────────────────────
    // Liste de MIME types au format "type/subtype".
    // Validation syntaxique fine : doit contenir un '/'.
    // (Le schema_validator de l'Étape 3 fera la validation sémantique
    // plus poussée si besoin.)
    if (has_key(node, "allowed_mime_types")) {
        cfg.allowed_mime_types = parse_string_list(node, "allowed_mime_types",
                                                   "file." + field_name);
        for (const auto& mime : cfg.allowed_mime_types) {
            if (mime.find('/') == std::string::npos) {
                throw sea::sea_errors_handling::YamlParsingException(
                    "[YAML PARSING EXCEPTION] Invalid MIME type '" + mime +
                    "' for field '" + field_name +
                    "' (expected format: 'type/subtype', e.g. 'image/png')."
                    );
            }
        }
    }

    // ── allowed_extensions ──────────────────────────────────
    // Normalisation à l'écriture :
    //   - point initial ajouté si absent ("png" → ".png")
    //   - lowercase ("PNG" → ".png")
    // Cela évite que FileFieldConfig::accepts_extension ait à le faire
    // côté lecture pour chaque comparaison.
    if (has_key(node, "allowed_extensions")) {
        auto raw_exts = parse_string_list(node, "allowed_extensions",
                                          "file." + field_name);

        cfg.allowed_extensions.reserve(raw_exts.size());
        for (auto& ext : raw_exts) {
            if (ext.empty()) {
                throw sea::sea_errors_handling::YamlParsingException(
                    "[YAML PARSING EXCEPTION] Empty extension in allowed_extensions of field '" +
                    field_name + "'."
                    );
            }

            std::string normalized;
            normalized.reserve(ext.size() + 1);
            if (ext.front() != '.') {
                normalized.push_back('.');
            }
            for (char c : ext) {
                normalized.push_back(
                    static_cast<char>(std::tolower(static_cast<unsigned char>(c)))
                    );
            }

            // Sécurité : une extension ne doit contenir qu'un seul point
            // au début (ex: ".tar.gz" est explicitement supporté car
            // tar.gz est une extension composée légitime — on accepte
            // tous les '.' internes, on bloque uniquement les '/' et '\').
            if (normalized.find('/') != std::string::npos ||
                normalized.find('\\') != std::string::npos) {
                throw sea::sea_errors_handling::YamlParsingException(
                    "[YAML PARSING EXCEPTION] Extension contains an invalid path separator: '" +
                    ext + "' (field '" + field_name + "')."
                    );
            }

            cfg.allowed_extensions.push_back(std::move(normalized));
        }
    }

    // ── storage_path ────────────────────────────────────────
    // Chemin RELATIF dans le storage. Validation profonde (pas de '../',
    // pas de path absolu) déléguée au schema_validator (Étape 3) pour
    // garder le parser focalisé sur la structure.
    cfg.storage_path = get_or_default<std::string>(node, "storage_path", "");

    // ── on_delete ───────────────────────────────────────────
    if (has_key(node, "on_delete")) {
        const auto raw = node["on_delete"].as<std::string>();
        const auto parsed = sea::domain::on_delete_file_from_string(raw);
        if (!parsed.has_value()) {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] Invalid on_delete for field '" + field_name +
                "': '" + raw + "' (accepted values: cascade, set_null, restrict)."
                );
        }
        cfg.on_delete = *parsed;
    }
    // sinon : laisse le défaut (Cascade) défini dans FileFieldConfig.

    return cfg;
}

// ─────────────────────────────────────────────────────────────
// parse_storage_config_node
//
// Parse le sous-bloc `storage:` au niveau service. Ce bloc est
// OPTIONNEL : un YAML sans bloc storage est valide (le FileServiceFactory
// appliquera un fallback ./uploads si le schema a des champs File).
//
// Tous les sous-champs sauf `backend` sont egalement optionnels :
// les defauts de StorageConfig (cf. storage_config.h) s'appliquent.
//
// Note sur les modes octaux : YAML lit naturellement les nombres en
// base 10. Pour declarer 0640 en YAML, deux options :
//   - en string : "0640"  → on parse en base 8
//   - en number : 0640    → en YAML c'est lu comme 640 base 10 (mauvais)
// On accepte les deux : si number, on suppose deja en decimal logique
// (640 == 0o1200 ce qui est faux). Donc on traite TOUJOURS comme octal :
// on parse en base 8 systematiquement, sous forme string ou int.
// ─────────────────────────────────────────────────────────────
sea::domain::StorageConfig
YamlSchemaParser::parse_storage_config_node(const YAML::Node& node) const
{
    if (!node || !node.IsMap()) {
        throw sea::sea_errors_handling::YamlParsingException(
            "[YAML PARSING EXCEPTION] The 'storage:' block must be an object (map)."
            );
    }

    sea::domain::StorageConfig cfg{};

    // ── backend ─────────────────────────────────────────────
    // Default : Filesystem. Seule valeur supportee aujourd'hui.
    if (has_key(node, "backend")) {
        const auto raw = node["backend"].as<std::string>();
        std::string lower;
        lower.reserve(raw.size());
        for (char c : raw) {
            lower.push_back(static_cast<char>(
                std::tolower(static_cast<unsigned char>(c))));
        }
        if (lower == "filesystem") {
            cfg.backend = sea::domain::StorageBackend::Filesystem;
        }
        // Futurs backends : "s3", "gcs", "azure" — ajouter ici.
        else {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] Invalid storage.backend: '" + raw +
                "' (currently accepted values: filesystem)."
                );
        }
    }
    // Sinon : default Filesystem (cf. StorageConfig).

    // ── root_path ───────────────────────────────────────────
    // Pour Filesystem, root_path est important. On l'accepte vide ici
    // (le validator ou FilesystemStorage levera plus loin si besoin).
    if (has_key(node, "root_path")) {
        cfg.root_path = node["root_path"].as<std::string>();
    }

    // ── file_mode (octal) ───────────────────────────────────
    // Parser en base 8 quelle que soit la forme YAML (string ou int).
    if (has_key(node, "file_mode")) {
        try {
            const auto raw = node["file_mode"].as<std::string>();
            cfg.file_mode = static_cast<std::uint32_t>(
                std::stoul(raw, nullptr, 8));
        } catch (const std::exception& e) {
            throw sea::sea_errors_handling::YamlParsingException(
                std::string("[YAML PARSING EXCEPTION] Invalid storage.file_mode: ") + e.what()
                );
        }
    }

    // ── directory_mode (octal) ──────────────────────────────
    if (has_key(node, "directory_mode")) {
        try {
            const auto raw = node["directory_mode"].as<std::string>();
            cfg.directory_mode = static_cast<std::uint32_t>(
                std::stoul(raw, nullptr, 8));
        } catch (const std::exception& e) {
            throw sea::sea_errors_handling::YamlParsingException(
                std::string("[YAML PARSING EXCEPTION] Invalid storage.directory_mode: ") + e.what()
                );
        }
    }

    return cfg;
}

domain::DatabaseDialect YamlSchemaParser::parse_database_dialect_node(const std::string& value) const
{
    if (value == "mysql")
        return domain::DatabaseDialect::MySQL;

    if (value == "postgresql")
        return domain::DatabaseDialect::PostgreSQL;

    if (value == "sqlite")
        return domain::DatabaseDialect::SQLite;

    if (value == "sqlserver")
        return domain::DatabaseDialect::SQLServer;

    throw sea::sea_errors_handling::YamlParsingException("[YAML PARSING EXCEPTION] Dialecte SQL inconnu: " + value);
}

sea::domain::Relation YamlSchemaParser::parse_relation_node(const YAML::Node& node) const {
    if (!node || !node.IsMap()) {
        throw sea::sea_errors_handling::YamlParsingException(
            "[YAML PARSING EXCEPTION] A YAML relation must be an object."
            );
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
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] The many_to_many relation '" + relation.name +
                "' must define 'pivot_table'."
                );
        }

        if (relation.source_fk_column.empty()) {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] The many_to_many relation '" + relation.name +
                "' must define 'source_fk_column'."
                );
        }

        if (relation.target_fk_column.empty()) {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] The many_to_many relation '" + relation.name +
                "' must define 'target_fk_column'."
                );
        }
    }
    return relation;
}

sea::domain::security::SecurityConfig YamlSchemaParser::parse_security_node(const YAML::Node &node) const
{
    using SecurityConfig = sea::domain::security::SecurityConfig;
    if (!node || !node.IsMap()) {
        throw sea::sea_errors_handling::YamlParsingException(
            "[YAML PARSING EXCEPTION] A YAML field must be an object."
            );
    }
    SecurityConfig security_config = SecurityConfig::safe_defaults();

    // Authentication
    if (const YAML::Node auth_node = node["authentication"]) {
        if (!auth_node.IsMap()) {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] The 'authentication' field must be an object in service '"
                );
        }
        security_config.set_authentication(parse_auth_node(auth_node));
    }

    // Cors
    if (const YAML::Node cors_node = node["cors"]) {
        if (!cors_node.IsMap()) {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] The 'cors' field must be an object in security '"
                );
        }
        security_config.set_cors(parse_cors_node(cors_node));
    }

    // Rate Limits
    if (const YAML::Node rate_limits_node = node["rate_limits"]) {
        if (!rate_limits_node.IsSequence()) {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] The 'rate_limits' field must be a list in service '"
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
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] The 'headers' field must be an object in service '"
                );
        }
        security_config.set_security_headers(parse_security_headers_node(headers_node));
    }

    // HTTP Limits
    if (const YAML::Node limits_node = node["http_limits"]) {
        if (!limits_node.IsMap()) {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] The 'http_limits' field must be an object in service '"
                );
        }
        security_config.set_http_limits(parse_http_limits_node(limits_node));
    }

    // Validation finale de cohérence
    try {
        security_config.validate();
    } catch (const std::exception& e) {
        throw sea::sea_errors_handling::YamlParsingException(
            "[YAML PARSING EXCEPTION] Invalid security configuration in service '"
            );
    }

    return security_config;
}
domain::security::AuthentificationConfig YamlSchemaParser::parse_auth_node(const YAML::Node &node) const
{
    if (!node || !node.IsMap()) {
        throw sea::sea_errors_handling::YamlParsingException(
            "[YAML PARSING EXCEPTION] A YAML field must be an object."
            );
    }
    using AuthentificationConfig = domain::security::AuthentificationConfig;
    AuthentificationConfig auth_config;
    const std::string type_str =
        get_or_default<std::string>(node, "type", "none");
    try {
        auth_config.set_type(domain::security::auth_type_from_string(type_str));
    } catch (const std::exception& e) {
        throw sea::sea_errors_handling::YamlParsingException(
            std::string("[YAML PARSING EXCEPTION] Invalid authentication type in service '") + e.what()
            );
    }

    // JWT
    if (auth_config.type() == domain::security::AuthType::Jwt) {
        const std::string algo_str =
            get_or_default<std::string>(node, "algorithm", "HS256");
        try {
            auth_config.set_jwt_algorithm(domain::security::jwt_algorithm_from_string(algo_str));
        } catch (const std::exception& e) {
            throw sea::sea_errors_handling::YamlParsingException(
                std::string("[YAML PARSING EXCEPTION] Invalid JWT algorithm in service '") + e.what()
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
        // ── token_delivery ──
        if (node["token_delivery"]) {
            const auto delivery_str = node["token_delivery"].as<std::string>();
            try {
                auth_config.set_token_delivery(
                    domain::security::token_delivery_from_string(delivery_str)
                    );
            } catch (const std::exception& e) {
                throw std::runtime_error(
                    std::string("token_delivery invalide : ") + e.what()
                    );
            }
        }

        // ──  cookie ──
        if (node["cookie"]) {
            auth_config.set_cookie_config(parse_cookie_config(node["cookie"]));
        }

        // ── [AJOUT ETAPE 1.1] token_tracking ──
        if (node["token_tracking"]) {
            auth_config.set_token_tracking(
                parse_token_tracking_config(node["token_tracking"])
                );
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
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] Le champ 'cors.allowed_origins' doit être une liste dans service "
                );
        }
        std::vector<std::string> list;
        for (const auto& o : origins) {
            list.push_back(o.as<std::string>());
        }
        cors.set_allowed_origins(std::move(list));
    }

    // allowed_methods
    if (const YAML::Node methods = node["allowed_methods"]) {
        if (!methods.IsSequence()) {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] Le champ 'cors.allowed_methods' doit être une liste dans service "
                );
        }
        std::vector<sea::domain::http::HttpMethod> list;
        for (const auto& m : methods) {
            try {
                list.push_back(sea::domain::http::from_string(m.as<std::string>()));
            } catch (const std::exception& e) {
                throw sea::sea_errors_handling::YamlParsingException(
                    std::string("[YAML PARSING EXCEPTION] Méthode HTTP invalide dans 'cors.allowed_methods' du service : ") + e.what()
                    );
            }
        }
        cors.set_allowed_methods(std::move(list));
    }

    // allowed_headers
    if (const YAML::Node headers = node["allowed_headers"]) {
        if (!headers.IsSequence()) {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] Le champ 'cors.allowed_headers' doit être une liste dans service "
                );
        }
        std::vector<std::string> list;
        for (const auto& h : headers) {
            list.push_back(h.as<std::string>());
        }
        cors.set_allowed_headers(std::move(list));
    }

    // exposed_headers
    if (const YAML::Node headers = node["exposed_headers"]) {
        if (!headers.IsSequence()) {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] Le champ 'cors.exposed_headers' doit être une liste dans service "
                );
        }
        std::vector<std::string> list;
        for (const auto& h : headers) {
            list.push_back(h.as<std::string>());
        }
        cors.set_exposed_headers(std::move(list));
    }

    // allow_credentials
    if (node["allow_credentials"]) {
        cors.set_allow_credentials(node["allow_credentials"].as<bool>());
    }

    // max_age
    if (node["max_age"]) {
        cors.set_max_age(parse_duration(node["max_age"].as<std::string>()));
    }

    return cors;
}

domain::security::RateLimitRule YamlSchemaParser::parse_rate_limite_rule_node(const YAML::Node &node) const
{
    using namespace sea::domain::security;

    if (!node || !node.IsMap()) {
        throw sea::sea_errors_handling::YamlParsingException(
            "[YAML PARSING EXCEPTION] Une règle 'rate_limits' doit être un objet"
            );
    }

    const std::string scope_str = require_string(node, "scope", "rate_limit_rule");
    RateLimitScope scope;
    try {
        scope = scope_from_string(scope_str);
    } catch (const std::exception& e) {
        throw sea::sea_errors_handling::YamlParsingException(
            std::string("[YAML PARSING EXCEPTION] Scope invalide pour 'rate_limits' : ") + e.what()
            );
    }

    if (!node["requests"]) {
        throw sea::sea_errors_handling::YamlParsingException(
            "[YAML PARSING EXCEPTION] Le champ 'requests' est obligatoire dans 'rate_limits'"
            );
    }
    if (!node["window"]) {
        throw sea::sea_errors_handling::YamlParsingException(
            "[YAML PARSING EXCEPTION] Le champ 'window' est obligatoire dans 'rate_limits'"
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
        throw sea::sea_errors_handling::YamlParsingException(
            std::string("[YAML PARSING EXCEPTION] Règle 'rate_limits' invalide ") + e.what()
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
            throw sea::sea_errors_handling::YamlParsingException(
                std::string("[YAML PARSING EXCEPTION] Valeur invalide pour 'http_limits.max_body_size' ") + e.what()
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
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] Preset de headers de sécurité inconnu Valeurs possibles: 'recommended', 'strict', 'none'."
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

domain::security::CookieConfig YamlSchemaParser::parse_cookie_config(const YAML::Node &node) const
{
    using namespace sea::domain::security;
    CookieConfig cfg = CookieConfig::safe_defaults();

    if (!node || !node.IsMap()) {
        return cfg;  // tous les defauts
    }

    if (node["domain"]) {
        cfg.set_domain(node["domain"].as<std::string>());
    }
    if (node["path"]) {
        cfg.set_path(node["path"].as<std::string>());
    }
    if (node["secure"]) {
        cfg.set_secure(node["secure"].as<bool>());
    }
    if (node["same_site"]) {
        const auto same_site_str = node["same_site"].as<std::string>();
        cfg.set_same_site(same_site_from_string(same_site_str));
    }
    if (node["access_token_name"]) {
        cfg.set_access_token_name(node["access_token_name"].as<std::string>());
    }
    if (node["refresh_token_name"]) {
        cfg.set_refresh_token_name(node["refresh_token_name"].as<std::string>());
    }

    return cfg;

}

domain::security::TokenTrackingConfig YamlSchemaParser::parse_token_tracking_config(const YAML::Node &node) const
{
    using namespace sea::domain::security;

    if (!node || !node.IsMap()) {
        return TokenTrackingConfig::disabled();
    }

    TokenTrackingConfig cfg;
    cfg.set_enabled(node["enabled"] ? node["enabled"].as<bool>() : true);

    if (node["refresh_table"]) {
        cfg.set_refresh_table(node["refresh_table"].as<std::string>());
    }
    if (node["revoked_table"]) {
        cfg.set_revoked_table(node["revoked_table"].as<std::string>());
    }

    // Sous-bloc cache
    if (node["cache"] && node["cache"].IsMap()) {
        const auto& cache_node = node["cache"];
        TokenTrackingConfig::CacheConfig cache;
        if (cache_node["enabled"]) {
            cache.enabled = cache_node["enabled"].as<bool>();
        }
        if (cache_node["ttl"]) {
            cache.ttl = parse_duration(cache_node["ttl"].as<std::string>());
        }
        if (cache_node["max_size"]) {
            cache.max_size = cache_node["max_size"].as<std::size_t>();
        }
        cfg.set_cache(cache);
    }

    // Sous-bloc rotation
    if (node["rotation"] && node["rotation"].IsMap()) {
        const auto& rot_node = node["rotation"];
        TokenTrackingConfig::RotationConfig rotation;
        if (rot_node["enabled"]) {
            rotation.enabled = rot_node["enabled"].as<bool>();
        }
        cfg.set_rotation(rotation);
    }

    // Sous-bloc auto_cleanup
    if (node["auto_cleanup"] && node["auto_cleanup"].IsMap()) {
        const auto& cleanup_node = node["auto_cleanup"];
        TokenTrackingConfig::AutoCleanupConfig cleanup;
        if (cleanup_node["enabled"]) {
            cleanup.enabled = cleanup_node["enabled"].as<bool>();
        }
        if (cleanup_node["interval"]) {
            cleanup.interval = parse_duration(cleanup_node["interval"].as<std::string>());
        }
        if (cleanup_node["keep_revoked_for"]) {
            cleanup.keep_revoked_for = parse_duration(
                cleanup_node["keep_revoked_for"].as<std::string>()
                );
        }
        cfg.set_auto_cleanup(cleanup);
    }

    return cfg;

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
    if (const YAML::Node preset = node["migrations"]) {

    }

    if (has_key(node, "migrations")) {
        const YAML::Node migration_node = node["migrations"];
        if (!migration_node.IsMap()) {
            throw sea::sea_errors_handling::YamlParsingException("[YAML PARSING EXCEPTION] 'migrations' doit être un objet.");
        }
        config.migrations.create_database_if_missing = get_or_default<bool>(
            migration_node, "create_database_if_missing",
            config.migrations.create_database_if_missing
            );
        config.migrations.enabled = get_or_default<bool>(
            migration_node, "enabled", config.migrations.enabled
            );
        config.migrations.mode = get_or_default<domain::MigrationMode>(
            migration_node, "mode", config.migrations.mode
            );

        // parser le sous-bloc 'seeds'
        if (has_key(migration_node, "seeds")) {
            config.migrations.seeds = parse_seeds_config_node(migration_node["seeds"]);
        }
    }


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

    throw sea::sea_errors_handling::YamlParsingException("[YAML PARSING EXCEPTION] Type de relation inconnu: '" + value + "'.");
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

    throw sea::sea_errors_handling::YamlParsingException("[YAML PARSING EXCEPTION] Valeur on_delete inconnue: '" + value + "'.");
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

    throw sea::sea_errors_handling::YamlParsingException("[YAML PARSING EXCEPTION] Type de base de donnees inconnu: '" + value + "'.");
}

// ═══════════════════════════════════════════════════════════════════════
// Module 3 : Access Control parsing
// ═══════════════════════════════════════════════════════════════════════

sea::domain::access_control::AccessControlConfig
YamlSchemaParser::parse_authorization_node(const YAML::Node& node) const
{
    using namespace sea::domain::access_control;

    if (!node || !node.IsMap()) {
        return AccessControlConfig::disabled();
    }

    AccessControlConfig config;

    // enabled
    if (has_key(node, "enabled")) {
        config.set_enabled(node["enabled"].as<bool>());
    }

    if (!config.enabled()) {
        return AccessControlConfig::disabled();
    }

    // default_policy
    if (has_key(node, "default_policy")) {
        const auto policy_str = node["default_policy"].as<std::string>();
        const auto policy = default_policy_from_string(policy_str);
        if (!policy.has_value()) {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] security.authorization.default_policy must be 'deny' or 'allow' (got '" +
                policy_str + "')"
                );
        }
        config.set_default_policy(*policy);
    } else {
        config.set_default_policy(DefaultPolicy::Deny);
    }

    // roles_claim_name
    if (has_key(node, "roles_claim_name")) {
        config.set_roles_claim_name(node["roles_claim_name"].as<std::string>());
    } else {
        config.set_roles_claim_name("role");
    }

    // admin_role
    if (has_key(node, "admin_role")) {
        config.set_admin_role(node["admin_role"].as<std::string>());
    } else {
        config.set_admin_role("admin");
    }

    // default_allow_admin
    if (has_key(node, "default_allow_admin")) {
        config.set_default_allow_admin(node["default_allow_admin"].as<bool>());
    } else {
        config.set_default_allow_admin(true);
    }

    // default_scope_field
    if (has_key(node, "default_scope_field")) {
        config.set_default_scope_field(node["default_scope_field"].as<std::string>());
    }

    // roles (catalogue)
    if (has_key(node, "roles") && node["roles"].IsSequence()) {
        std::vector<std::string> roles;
        for (const auto& r : node["roles"]) {
            roles.push_back(r.as<std::string>());
        }
        config.set_declared_roles(std::move(roles));
    }
    // abac_mode (service-level)
    if (has_key(node, "abac_mode")) {
        const auto mode_str = node["abac_mode"].as<std::string>();
        const auto mode = abac_mode_from_string(mode_str);
        if (!mode.has_value()) {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] authorization.abac_mode: invalid value '" + mode_str +
                "'. Valid values: permissive, strict"
                );
        }
        config.set_abac_mode(*mode);
    }
    // Validation finale (throw si incohérent)
    config.validate();

    return config;
}

sea::domain::access_control::EntityAccessControl
YamlSchemaParser::parse_entity_access_control_node(
    const YAML::Node& entity_node,
    const sea::domain::Entity& entity,
    const sea::domain::access_control::AccessControlConfig& global_config) const
{
    using namespace sea::domain::access_control;

    EntityAccessControl entity_ac;

    const std::string ctx = "entity '" + entity.name + "'";

    // scope_field au niveau entité
    if (has_key(entity_node, "scope_field")) {
        entity_ac.set_scope_field(entity_node["scope_field"].as<std::string>());
    }

    // owner_field au niveau entité
    if (has_key(entity_node, "owner_field")) {
        entity_ac.set_owner_field(entity_node["owner_field"].as<std::string>());
    }

    // Section access_control
    if (!has_key(entity_node, "access_control")) {
        return entity_ac;  // pas de règles, default_policy s'appliquera
    }

    const auto ac_node = entity_node["access_control"];
    if (!ac_node.IsMap()) {
        throw sea::sea_errors_handling::YamlParsingException(ctx + ".access_control must be a mapping");
    }

    // abac_mode override par entité (optionnel)
    if (has_key(ac_node, "abac_mode")) {
        const auto mode_str = ac_node["abac_mode"].as<std::string>();
        const auto mode = abac_mode_from_string(mode_str);

        if (!mode.has_value()) {
            throw sea::sea_errors_handling::YamlParsingException("[YAML PARSING EXCEPTION] " +
                                                                 ctx + ".access_control.abac_mode: invalid value '" + mode_str +
                                                                 "'. Valid values: permissive, strict"
                                                                 );
        }
        entity_ac.set_abac_mode_override(*mode);
    }

    // Détermine le scope_field effectif (entité OU défaut service)
    const std::string effective_scope_field =
        !entity_ac.scope_field().empty()
            ? entity_ac.scope_field()
            : global_config.default_scope_field();

    const std::string effective_owner_field = entity_ac.owner_field();

    // Parse chaque opération
    for (auto it = ac_node.begin(); it != ac_node.end(); ++it) {
        const auto op_name = it->first.as<std::string>();
        const auto op_node = it->second;

        // skip "abac_mode" (n'est pas une opération CRUD)
        if (op_name == "abac_mode") {
            continue;
        }
        const auto op = crud_operation_from_string(op_name);
        if (!op.has_value()) {
            throw sea::sea_errors_handling::YamlParsingException("[YAML PARSING EXCEPTION] " +
                                                                 ctx + ".access_control: unknown operation '" + op_name +
                                                                 "'. Valid operations: list, get_by_id, create, update, delete"
                                                                 );
        }

        if (!op_node.IsMap()) {
            throw sea::sea_errors_handling::YamlParsingException("[YAML PARSING EXCEPTION] " +
                                                                 ctx + ".access_control." + op_name + " must be a mapping"
                                                                 );
        }

        auto spec = parse_operation_access_control_node(
            op_node, entity.name, op_name,
            effective_scope_field, effective_owner_field,
            global_config
            );

        entity_ac.set_spec(*op, std::move(spec));
    }

    return entity_ac;
}

sea::domain::access_control::AccessControlSpec
YamlSchemaParser::parse_operation_access_control_node(
    const YAML::Node& op_node,
    const std::string& entity_name,
    const std::string& op_name,
    const std::string& effective_scope_field,
    const std::string& effective_owner_field,
    const sea::domain::access_control::AccessControlConfig& global_config) const
{
    using namespace sea::domain::access_control;

    const std::string ctx =
        "entity '" + entity_name + "'.access_control." + op_name;

    std::vector<PolicyCondition> generated_conditions;

    // ─── allow_roles ───
    if (has_key(op_node, "allow_roles")) {
        if (!op_node["allow_roles"].IsSequence()) {
            throw sea::sea_errors_handling::YamlParsingException("[YAML PARSING EXCEPTION] " + ctx + ".allow_roles must be a list");
        }

        std::vector<std::string> roles;
        for (const auto& r : op_node["allow_roles"]) {
            const auto role = r.as<std::string>();

            // Validation : le rôle doit être déclaré (si la liste existe)
            if (!global_config.declared_roles().empty() &&
                !global_config.is_role_declared(role)) {
                throw sea::sea_errors_handling::YamlParsingException("[YAML PARSING EXCEPTION] " +
                                                                     ctx + ".allow_roles: role '" + role +
                                                                     "' is not declared in authorization.roles"
                                                                     );
            }
            roles.push_back(role);
        }

        if (!roles.empty()) {
            generated_conditions.push_back(compile_allow_roles_shortcut(roles));
        }
    }

    // ─── same_scope ───
    if (has_key(op_node, "same_scope")) {
        const auto& ss_node = op_node["same_scope"];
        std::string scope_to_use;

        // Tente de parser comme bool, sinon comme string
        try {
            if (ss_node.as<bool>()) {
                scope_to_use = effective_scope_field;
            }
        } catch (const YAML::Exception&) {
            scope_to_use = ss_node.as<std::string>();
        }

        if (!scope_to_use.empty()) {
            if (effective_scope_field.empty()) {
                throw sea::sea_errors_handling::YamlParsingException("[YAML PARSING EXCEPTION] " +
                                                                     ctx + ".same_scope requires a scope_field. "
                                                                           "Define it at entity level or via authorization.default_scope_field"
                                                                     );
            }

            generated_conditions.push_back(
                compile_same_scope_shortcut(
                    scope_to_use,
                    global_config.default_allow_admin(),
                    global_config.admin_role()
                    )
                );
        }
    }

    // ─── own_resource ───
    if (has_key(op_node, "own_resource")) {
        const auto& or_node = op_node["own_resource"];
        std::string owner_to_use;

        try {
            if (or_node.as<bool>()) {
                owner_to_use = effective_owner_field;
            }
        } catch (const YAML::Exception&) {
            owner_to_use = or_node.as<std::string>();
        }

        if (!owner_to_use.empty()) {
            if (effective_owner_field.empty()) {
                throw sea::sea_errors_handling::YamlParsingException("[YAML PARSING EXCEPTION] " +
                                                                     ctx + ".own_resource requires an owner_field. "
                                                                           "Define it at entity level (owner_field: <field>)"
                                                                     );
            }

            generated_conditions.push_back(
                compile_own_resource_shortcut(
                    owner_to_use,
                    global_config.default_allow_admin(),
                    global_config.admin_role()
                    )
                );
        }
    }

    // ─── Combinaison finale ───
    if (generated_conditions.empty()) {
        return AccessControlSpec{};  // vide → default_policy au runtime
    }

    if (generated_conditions.size() == 1) {
        return AccessControlSpec(std::move(generated_conditions[0]));
    }

    // Plusieurs conditions → AND implicite
    return AccessControlSpec(
        PolicyCondition::all_of(std::move(generated_conditions))
        );
}

sea::domain::access_control::PolicyCondition
YamlSchemaParser::compile_allow_roles_shortcut(
    const std::vector<std::string>& roles) const
{
    using namespace sea::domain::access_control;

    auto pred = PolicyPredicate::make(
        PolicyValueRef::from_subject("roles"),
        PolicyOperator::Intersects,
        PolicyValueRef::from_literal_list(roles)
        );

    return PolicyCondition(std::move(pred));
}
// ─────────────────────────────────────────────────────────────
// parse_seeds_config_node
// ─────────────────────────────────────────────────────────────
sea::domain::SeedsConfig
YamlSchemaParser::parse_seeds_config_node(const YAML::Node& node) const
{
    sea::domain::SeedsConfig config;

    if (!node || !node.IsMap()) {
        return config;
    }

    config.enabled = get_or_default<bool>(node, "enabled", false);

    if (has_key(node, "mode")) {
        const auto mode_str = node["mode"].as<std::string>();
        const auto parsed = sea::domain::seeds_mode_from_string(mode_str);
        if (parsed.has_value()) {
            config.mode = *parsed;
        } else {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] seeds.mode: invalid value '" + mode_str +
                "'. Valid values: once, always"
                );
        }
    }

    if (has_key(node, "on_error")) {
        const auto policy_str = node["on_error"].as<std::string>();
        if (policy_str == "abort") {
            config.on_error = sea::domain::SeedsErrorPolicy::Abort;
        } else if (policy_str == "continue") {
            config.on_error = sea::domain::SeedsErrorPolicy::Continue;
        } else {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] seeds.on_error: invalid value '" + policy_str +
                "'. Valid values: continue, abort"
                );
        }
    }

    return config;
}
// ─────────────────────────────────────────────────────────────────────
// Pagination — un parser par mode
// ─────────────────────────────────────────────────────────────────────

sea::domain::PaginationConfig
YamlSchemaParser::parse_pagination_node(const YAML::Node& node,
                                        const std::string& entity_name) const {
    sea::domain::PaginationConfig config{};

    if (has_key(node, "page")) {
        const YAML::Node sub = node["page"];
        if (!sub.IsMap()) {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] 'pagination.page' doit etre un objet dans l'entite '" + entity_name + "'."
                );
        }
        config.page = parse_page_pagination_node(sub, entity_name);
    }

    if (has_key(node, "offset")) {
        const YAML::Node sub = node["offset"];
        if (!sub.IsMap()) {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] 'pagination.offset' doit etre un objet dans l'entite '" + entity_name + "'."
                );
        }
        config.offset = parse_offset_pagination_node(sub, entity_name);
    }

    if (has_key(node, "cursor")) {
        const YAML::Node sub = node["cursor"];
        if (!sub.IsMap()) {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] 'pagination.cursor' doit etre un objet dans l'entite '" + entity_name + "'."
                );
        }
        config.cursor = parse_cursor_pagination_node(sub, entity_name);
    }

    if (!config.any()) {
        throw sea::sea_errors_handling::YamlParsingException(
            "[YAML PARSING EXCEPTION] 'pagination' de l'entite '" + entity_name +
            "' doit contenir au moins un mode parmi 'page', 'offset' ou 'cursor'."
            );
    }

    return config;
}

sea::domain::PagePagination
YamlSchemaParser::parse_page_pagination_node(const YAML::Node& node,
                                             const std::string& entity_name) const {
    sea::domain::PagePagination cfg{};

    const std::string ctx = "pagination.page de l'entite '" + entity_name + "'";

    cfg.default_page_size = get_or_default<std::size_t>(node, "default_page_size", cfg.default_page_size);
    cfg.max_page_size     = get_or_default<std::size_t>(node, "max_page_size",     cfg.max_page_size);

    if (has_key(node, "default_sort")) {
        cfg.default_sort = node["default_sort"].as<std::string>();
    }

    cfg.sortable_fields = parse_string_list(node, "sortable_fields", ctx);

    return cfg;
}

sea::domain::OffsetPagination
YamlSchemaParser::parse_offset_pagination_node(const YAML::Node& node,
                                               const std::string& entity_name) const {
    sea::domain::OffsetPagination cfg{};

    const std::string ctx = "pagination.offset de l'entite '" + entity_name + "'";

    cfg.default_limit = get_or_default<std::size_t>(node, "default_limit", cfg.default_limit);
    cfg.max_limit     = get_or_default<std::size_t>(node, "max_limit",     cfg.max_limit);

    if (has_key(node, "default_sort")) {
        cfg.default_sort = node["default_sort"].as<std::string>();
    }

    cfg.sortable_fields = parse_string_list(node, "sortable_fields", ctx);

    return cfg;
}

sea::domain::CursorPagination
YamlSchemaParser::parse_cursor_pagination_node(const YAML::Node& node,
                                               const std::string& entity_name) const {
    sea::domain::CursorPagination cfg{};

    cfg.default_limit = get_or_default<std::size_t>(node, "default_limit", cfg.default_limit);
    cfg.max_limit     = get_or_default<std::size_t>(node, "max_limit",     cfg.max_limit);

    cfg.cursor_field = require_string(node, "cursor_field",
                                      ("pagination.cursor de l'entite '" + entity_name + "'").c_str());
    cfg.sort         = require_string(node, "sort",
                              ("pagination.cursor de l'entite '" + entity_name + "'").c_str());

    return cfg;
}

// ─────────────────────────────────────────────────────────────
// yaml_node_to_seed_value
//
// Convertit un YAML scalar en SeedValue (variant).
// Pour V1 : on stocke tout en string (le SeedOrchestrator fera
// la conversion en runtime::DynamicValue selon le type du field).
// Plus simple et evite les ambiguites de parsing YAML.
// ─────────────────────────────────────────────────────────────
sea::domain::SeedValue
YamlSchemaParser::yaml_node_to_seed_value(const YAML::Node& node) const
{
    if (!node || node.IsNull()) {
        return std::monostate{};
    }

    if (!node.IsScalar()) {
        // Pas de support pour les structures imbriquees
        return std::monostate{};
    }

    // Tente bool en premier (true/false)
    try {
        const auto s = node.as<std::string>();
        if (s == "true" || s == "false" || s == "True" || s == "False"
            || s == "TRUE" || s == "FALSE") {
            return s == "true" || s == "True" || s == "TRUE";
        }
    } catch (...) {}

    // Tente int
    try {
        return node.as<std::int64_t>();
    } catch (...) {}

    // Tente double
    try {
        return node.as<double>();
    } catch (...) {}

    // Fallback : string (cas le plus frequent pour les seeds)
    try {
        return node.as<std::string>();
    } catch (...) {}

    return std::monostate{};
}

// ─────────────────────────────────────────────────────────────
// parse_seed_record_node
//
// Parse un seul seed record d'une entity.
// Detecte automatiquement les champs M2M (basés sur entity.relations)
// et les met dans seed.m2m_relations au lieu de seed.values.
// ─────────────────────────────────────────────────────────────
sea::domain::SeedRecord
YamlSchemaParser::parse_seed_record_node(
    const YAML::Node& seed_node,
    const sea::domain::Entity& entity) const
{
    sea::domain::SeedRecord record;

    if (!seed_node || !seed_node.IsMap()) {
        throw sea::sea_errors_handling::YamlParsingException(
            "[YAML PARSING EXCEPTION] Un seed de l'entite '" + entity.name + "' doit etre un objet."
            );
    }

    // Construire le set des noms de relations M2M de l'entity
    std::set<std::string> m2m_relation_names;
    for (const auto& rel : entity.relations) {
        if (rel.kind == sea::domain::RelationKind::ManyToMany) {
            m2m_relation_names.insert(rel.name);
        }
    }

    for (auto it = seed_node.begin(); it != seed_node.end(); ++it) {
        const auto key = it->first.as<std::string>();
        const auto& value_node = it->second;

        // ── Cas 1 : alias (special) ──
        if (key == "alias") {
            record.alias = value_node.as<std::string>("");
            continue;
        }

        // ── Cas 2 : relation M2M (sequence d'aliases) ──
        if (m2m_relation_names.count(key)) {
            if (!value_node.IsSequence()) {
                throw sea::sea_errors_handling::YamlParsingException(
                    "[YAML PARSING EXCEPTION] Le champ M2M '" + key + "' dans seed de '" + entity.name +
                    "' doit etre une liste d'aliases"
                    );
            }
            std::vector<std::string> aliases;
            for (const auto& alias_node : value_node) {
                aliases.push_back(alias_node.as<std::string>(""));
            }
            record.m2m_relations[key] = std::move(aliases);
            continue;
        }

        // ── Cas 3 : champ simple (FK ou attribute) ──
        record.values[key] = yaml_node_to_seed_value(value_node);
    }

    return record;
}

sea::domain::access_control::PolicyCondition
YamlSchemaParser::compile_same_scope_shortcut(
    const std::string& scope_field,
    bool allow_admin,
    const std::string& admin_role) const
{
    using namespace sea::domain::access_control;

    if (scope_field.empty()) {
        throw sea::sea_errors_handling::YamlParsingException("[YAML PARSING EXCEPTION] same_scope requires a non-empty scope_field");
    }

    // subject.attributes.<scope_field> == resource.attributes.<scope_field>
    const std::string path = "attributes." + scope_field;

    auto pred = PolicyPredicate::make(
        PolicyValueRef::from_subject(path),
        PolicyOperator::Equals,
        PolicyValueRef::from_resource(path)
        );

    PolicyCondition condition(std::move(pred));

    if (!allow_admin) {
        return condition;
    }

    // Admin bypass : (admin) OR (same_scope check)
    auto admin_check = PolicyCondition(PolicyPredicate::make(
        PolicyValueRef::from_subject("roles"),
        PolicyOperator::Contains,
        PolicyValueRef::from_literal(admin_role)
        ));

    std::vector<PolicyCondition> children;
    children.push_back(std::move(admin_check));
    children.push_back(std::move(condition));

    return PolicyCondition::any_of(std::move(children));
}

sea::domain::access_control::PolicyCondition
YamlSchemaParser::compile_own_resource_shortcut(
    const std::string& owner_field,
    bool allow_admin,
    const std::string& admin_role) const
{
    using namespace sea::domain::access_control;

    if (owner_field.empty()) {
        throw sea::sea_errors_handling::YamlParsingException("[YAML PARSING EXCEPTION] own_resource requires a non-empty owner_field");
    }

    // subject.id == resource.<owner_field>
    // Si owner_field == "id" → resource.id (champ direct)
    // Sinon → resource.attributes.<owner_field>
    const std::string resource_path =
        (owner_field == "id") ? "id" : ("attributes." + owner_field);

    auto pred = PolicyPredicate::make(
        PolicyValueRef::from_subject("id"),
        PolicyOperator::Equals,
        PolicyValueRef::from_resource(resource_path)
        );

    PolicyCondition condition(std::move(pred));

    if (!allow_admin) {
        return condition;
    }

    // Admin bypass
    auto admin_check = PolicyCondition(PolicyPredicate::make(
        PolicyValueRef::from_subject("roles"),
        PolicyOperator::Contains,
        PolicyValueRef::from_literal(admin_role)
        ));

    std::vector<PolicyCondition> children;
    children.push_back(std::move(admin_check));
    children.push_back(std::move(condition));

    return PolicyCondition::any_of(std::move(children));
}

// ─────────────────────────────────────────────────────────────
// Section logging — helpers de parsing (etape 2.1 Sujet 2)
// ─────────────────────────────────────────────────────────────

sea::domain::logging::RotationConfig
YamlSchemaParser::parse_rotation_node(const YAML::Node& node) const
{
    using namespace sea::domain::logging;
    RotationConfig rot;

    if (!node || !node.IsMap()) return rot;

    if (has_key(node, "max_size")) {
        // parse_size retourne uint64_t, on cast en size_t
        rot.max_size_bytes =
            static_cast<std::size_t>(parse_size(node["max_size"].as<std::string>()));
    }
    if (has_key(node, "time_pattern")) {
        try {
            rot.time_pattern = time_pattern_from_string(
                node["time_pattern"].as<std::string>()
                );
        } catch (const std::exception& e) {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] logging.sinks[].rotation.time_pattern: "
                + std::string(e.what())
                );
        }
    }
    if (has_key(node, "max_files")) {
        rot.max_files = node["max_files"].as<std::size_t>();
    }
    if (has_key(node, "compress")) {
        rot.compress = node["compress"].as<bool>();
    }

    return rot;
}

sea::domain::logging::SinkConfig
YamlSchemaParser::parse_sink_node(const YAML::Node& node) const
{
    using namespace sea::domain::logging;

    if (!node || !node.IsMap()) {
        throw sea::sea_errors_handling::YamlParsingException(
            "[YAML PARSING EXCEPTION] logging.sinks: chaque element doit etre un objet"
            );
    }

    SinkConfig sink;

    if (!has_key(node, "type")) {
        throw sea::sea_errors_handling::YamlParsingException(
            "[YAML PARSING EXCEPTION] logging.sinks: 'type' manquant (attendu: console | file)"
            );
    }
    try {
        sink.type = sink_type_from_string(node["type"].as<std::string>());
    } catch (const std::exception& e) {
        throw sea::sea_errors_handling::YamlParsingException(
            "[YAML PARSING EXCEPTION] logging.sinks.type: " + std::string(e.what())
            );
    }

    if (has_key(node, "format")) {
        try {
            sink.format = log_format_from_string(node["format"].as<std::string>());
        } catch (const std::exception& e) {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] logging.sinks.format: " + std::string(e.what())
                );
        }
    }
    if (has_key(node, "enabled")) {
        sink.enabled = node["enabled"].as<bool>();
    }
    if (has_key(node, "path")) {
        sink.path = node["path"].as<std::string>();
    }
    if (has_key(node, "rotation")) {
        sink.rotation = parse_rotation_node(node["rotation"]);
    }

    return sink;
}

sea::domain::logging::AsyncConfig
YamlSchemaParser::parse_async_node(const YAML::Node& node) const
{
    using namespace sea::domain::logging;
    AsyncConfig cfg;

    if (!node || !node.IsMap()) return cfg;

    if (has_key(node, "enabled")) {
        cfg.enabled = node["enabled"].as<bool>();
    }
    if (has_key(node, "queue_size")) {
        cfg.queue_size = node["queue_size"].as<std::size_t>();
    }
    if (has_key(node, "overflow_policy")) {
        const auto policy = node["overflow_policy"].as<std::string>();
        if (policy == "block") {
            cfg.overflow_policy = AsyncConfig::OverflowPolicy::Block;
        } else if (policy == "overrun_oldest") {
            cfg.overflow_policy = AsyncConfig::OverflowPolicy::OverrunOldest;
        } else {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] logging.async.overflow_policy invalide: '"
                + policy + "' (attendu: block | overrun_oldest)"
                );
        }
    }

    return cfg;
}

sea::domain::logging::LoggingConfig
YamlSchemaParser::parse_logging_node(const YAML::Node& node) const
{
    using namespace sea::domain::logging;

    if (!node || !node.IsMap()) {
        // Section absente / mal formee -> defauts (console texte info)
        return LoggingConfig::safe_defaults();
    }

    LoggingConfig cfg;

    if (has_key(node, "enabled")) {
        cfg.set_enabled(node["enabled"].as<bool>());
    } else {
        cfg.set_enabled(true);
    }

    if (has_key(node, "level")) {
        try {
            cfg.set_level(log_level_from_string(node["level"].as<std::string>()));
        } catch (const std::exception& e) {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] logging.level: " + std::string(e.what())
                );
        }
    } else {
        cfg.set_level(LogLevel::Info);
    }

    // Modules : map name -> level
    if (has_key(node, "modules")) {
        const YAML::Node modules_node = node["modules"];
        if (!modules_node.IsMap()) {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] logging.modules doit etre un objet"
                );
        }
        for (const auto& kv : modules_node) {
            const auto module_name = kv.first.as<std::string>();
            const auto level_str   = kv.second.as<std::string>();
            try {
                cfg.set_module_level(module_name, log_level_from_string(level_str));
            } catch (const std::exception& e) {
                throw sea::sea_errors_handling::YamlParsingException(
                    "[YAML PARSING EXCEPTION] logging.modules['" + module_name +
                    "']: " + std::string(e.what())
                    );
            }
        }
    }

    // Sinks : tableau
    if (has_key(node, "sinks")) {
        const YAML::Node sinks_node = node["sinks"];
        if (!sinks_node.IsSequence()) {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] logging.sinks doit etre une liste"
                );
        }
        std::vector<SinkConfig> sinks;
        for (const auto& sink_node : sinks_node) {
            sinks.push_back(parse_sink_node(sink_node));
        }
        cfg.set_sinks(std::move(sinks));
    } else {
        // Pas de sinks declares -> un sink console par defaut
        SinkConfig console;
        console.type    = SinkType::Console;
        console.format  = LogFormat::Text;
        console.enabled = true;
        cfg.add_sink(std::move(console));
    }

    if (has_key(node, "flush_level")) {
        try {
            cfg.set_flush_level(log_level_from_string(node["flush_level"].as<std::string>()));
        } catch (const std::exception& e) {
            throw sea::sea_errors_handling::YamlParsingException(
                "[YAML PARSING EXCEPTION] logging.flush_level: " + std::string(e.what())
                );
        }
    }

    if (has_key(node, "async")) {
        cfg.set_async(parse_async_node(node["async"]));
    }

    return cfg;
}

// =====================================================================
//                    HELPERS DE PARSING
// =====================================================================

std::chrono::seconds
YamlSchemaParser::parse_duration(
    const std::string& s) const
{
    if (s.empty()) {
        throw sea::sea_errors_handling::YamlParsingException("[YAML PARSING EXCEPTION] Durée vide");
    }

    std::size_t suffix_pos = 0;
    while (suffix_pos < s.size() && std::isdigit(static_cast<unsigned char>(s[suffix_pos]))) {
        ++suffix_pos;
    }

    if (suffix_pos == 0) {
        throw sea::sea_errors_handling::YamlParsingException("[YAML PARSING EXCEPTION] Durée invalide (pas de nombre): '" + s + "'");
    }

    const std::uint64_t number = std::stoull(s.substr(0, suffix_pos));
    const std::string suffix = s.substr(suffix_pos);

    using namespace std::chrono;
    if (suffix.empty() || suffix == "s") return seconds(number);
    if (suffix == "m") return seconds(number * 60);
    if (suffix == "h") return seconds(number * 3600);
    if (suffix == "d") return seconds(number * 86400);

    throw sea::sea_errors_handling::YamlParsingException("[YAML PARSING EXCEPTION] Suffixe de durée inconnu: '" + suffix + "' dans '" + s + "'");
}

std::uint64_t
YamlSchemaParser::parse_size(
    const std::string& s) const
{
    if (s.empty()) {
        throw sea::sea_errors_handling::YamlParsingException("[YAML PARSING EXCEPTION] Taille vide");
    }

    std::size_t suffix_pos = 0;
    while (suffix_pos < s.size() &&
           (std::isdigit(static_cast<unsigned char>(s[suffix_pos])) || s[suffix_pos] == '.')) {
        ++suffix_pos;
    }

    if (suffix_pos == 0) {
        throw sea::sea_errors_handling::YamlParsingException("[YAML PARSING EXCEPTION] Taille invalide (pas de nombre): '" + s + "'");
    }

    const std::uint64_t number = std::stoull(s.substr(0, suffix_pos));
    const std::string suffix = s.substr(suffix_pos);

    if (suffix.empty() || suffix == "B") return number;
    if (suffix == "KB" || suffix == "K") return number * 1024;
    if (suffix == "MB" || suffix == "M") return number * 1024 * 1024;
    if (suffix == "GB" || suffix == "G") return number * 1024ULL * 1024 * 1024;

    throw sea::sea_errors_handling::YamlParsingException("[YAML PARSING EXCEPTION] Suffixe de taille inconnu: '" + suffix + "' dans '" + s + "'");
}

} // namespace sea::infrastructure::yaml