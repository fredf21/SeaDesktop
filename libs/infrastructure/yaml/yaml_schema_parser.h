#pragma once

#include <string>

#include <yaml-cpp/yaml.h>

#include "project.h"
#include "service.h"
#include "entity.h"
#include "field.h"
#include "file_field_config.h"   // NEW : FileFieldConfig + OnDeleteFile
#include "storage_config.h"      // NEW : StorageConfig
#include "relation.h"
#include "database_config.h"

namespace sea::infrastructure::yaml {

// ─────────────────────────────────────────────────────────────
// YamlSchemaParser
//
// Convertit un document YAML en objets du domaine.
//
// Point d'entrée principal du MVP :
//   model.yaml -> Project
//
// Plus tard, pourra aussi servir pour :
// - import partiel d'un service
// - outils CLI
// - UI Qt
// ─────────────────────────────────────────────────────────────
class YamlSchemaParser {
public:
    [[nodiscard]] sea::domain::Project parse_project_file(const std::string& file_path) const;
    [[nodiscard]] sea::domain::Service parse_service_file(const std::string& file_path) const;

private:
    [[nodiscard]] sea::domain::Project parse_project_node(const YAML::Node& root) const;
    [[nodiscard]] sea::domain::Service parse_service_node(const YAML::Node& node) const;

    //Parser La securitE
    [[nodiscard]] domain::security::SecurityConfig parse_security_node(const YAML::Node& node) const;
    [[nodiscard]] domain::security::AuthentificationConfig parse_auth_node(const YAML::Node& node) const;
    [[nodiscard]] domain::security::CorsConfig parse_cors_node(const YAML::Node& node) const;
    [[nodiscard]] domain::security::RateLimitRule parse_rate_limite_rule_node(const YAML::Node& node) const;
    [[nodiscard]] domain::security::HttpLimits parse_http_limits_node(const YAML::Node& node) const;
    [[nodiscard]] domain::security::SecurityHeaders parse_security_headers_node(const YAML::Node& node) const;
    [[nodiscard]] domain::security::CookieConfig parse_cookie_config(const YAML::Node& node) const;
    [[nodiscard]] domain::security::TokenTrackingConfig parse_token_tracking_config(const YAML::Node& node) const;

    // Parser l'autorisation
    [[nodiscard]] sea::domain::access_control::AccessControlConfig parse_authorization_node(const YAML::Node& node) const;

    [[nodiscard]] sea::domain::access_control::EntityAccessControl
    parse_entity_access_control_node(
        const YAML::Node& entity_node,
        const sea::domain::Entity& entity,
        const sea::domain::access_control::AccessControlConfig& global_config
        ) const;

    [[nodiscard]] sea::domain::access_control::AccessControlSpec
    parse_operation_access_control_node(
        const YAML::Node& op_node,
        const std::string& entity_name,
        const std::string& op_name,
        const std::string& effective_scope_field,
        const std::string& effective_owner_field,
        const sea::domain::access_control::AccessControlConfig& global_config
        ) const;

    // Pagination — un parser par mode pour ne pas mélanger les paradigmes
    [[nodiscard]] sea::domain::PaginationConfig
    parse_pagination_node(const YAML::Node& node,
                          const std::string& entity_name) const;
    [[nodiscard]] sea::domain::PagePagination
    parse_page_pagination_node(const YAML::Node& node,
                               const std::string& entity_name) const;
    [[nodiscard]] sea::domain::OffsetPagination
    parse_offset_pagination_node(const YAML::Node& node,
                                 const std::string& entity_name) const;
    [[nodiscard]] sea::domain::CursorPagination
    parse_cursor_pagination_node(const YAML::Node& node,
                                 const std::string& entity_name) const;


    // Compilation des shortcuts en PolicyCondition
    [[nodiscard]] sea::domain::access_control::PolicyCondition
    compile_allow_roles_shortcut(const std::vector<std::string>& roles) const;

    [[nodiscard]] sea::domain::access_control::PolicyCondition
    compile_same_scope_shortcut(
        const std::string& scope_field,
        bool allow_admin,
        const std::string& admin_role
        ) const;

    [[nodiscard]] sea::domain::access_control::PolicyCondition
    compile_own_resource_shortcut(
        const std::string& owner_field,
        bool allow_admin,
        const std::string& admin_role
        ) const;


    sea::domain::Entity parse_entity_node(
        const YAML::Node& node,
        const sea::domain::access_control::AccessControlConfig& global_config
        ) const;
    [[nodiscard]] sea::domain::Field parse_field_node(const YAML::Node& node) const;

    // Parser le sous-bloc `file:` d'un champ de type File.
    //
    // Appelé par parse_field_node uniquement quand field.type == FieldType::File.
    // Le sous-bloc `file:` est OBLIGATOIRE pour un champ File : un YAML qui
    // déclare `type: file` sans bloc `file:` est rejeté.
    //
    // Exemple de YAML accepté :
    //   file:
    //     max_size: 5MB
    //     allowed_mime_types: [image/png, image/jpeg]
    //     allowed_extensions: [.png, .jpg]
    //     storage_path: users/avatars
    //     on_delete: cascade
    //
    // Paramètres :
    //   node       : le nœud `file:` (sous-map du champ)
    //   field_name : nom du champ parent, utilisé pour les messages d'erreur
    [[nodiscard]] sea::domain::FileFieldConfig
    parse_file_field_config_node(const YAML::Node& node,
                                 const std::string& field_name) const;

    // Parser le sous-bloc `storage:` au niveau service.
    //
    // Optionnel : si manquant et que le service a au moins un champ
    // File, le FileServiceFactory appliquera un fallback automatique.
    //
    // Exemple de YAML accepte :
    //   storage:
    //     backend: filesystem          # filesystem (default), futur: s3, gcs, azure
    //     root_path: /var/lib/sea/uploads
    //     file_mode: 0640              # octal, default 0640
    //     directory_mode: 0750         # octal, default 0750
    //
    // Leve YamlParsingException si :
    //   - backend invalide
    //   - root_path manquant alors que le bloc est present
    //   - file_mode/directory_mode mal formes
    [[nodiscard]] sea::domain::StorageConfig
    parse_storage_config_node(const YAML::Node& node) const;

    [[nodiscard]] sea::domain::DatabaseDialect parse_database_dialect_node(const std::string& value) const;
    [[nodiscard]] sea::domain::Relation parse_relation_node(const YAML::Node& node) const;
    [[nodiscard]] sea::domain::DatabaseConfig parse_database_config_node(const YAML::Node& node) const;
    // parse le bloc 'seeds:' dans la config database
    [[nodiscard]] sea::domain::SeedsConfig
    parse_seeds_config_node(const YAML::Node& node) const;

    // parse un seul seed record dans une entity
    [[nodiscard]] sea::domain::SeedRecord
    parse_seed_record_node(
        const YAML::Node& seed_node,
        const sea::domain::Entity& entity
        ) const;

    [[nodiscard]] sea::domain::RelationKind parse_relation_kind(const std::string& value) const;
    [[nodiscard]] sea::domain::OnDelete parse_on_delete(const std::string& value) const;
    [[nodiscard]] sea::domain::DatabaseType parse_database_type(const std::string& value) const;

    [[nodiscard]] bool has_key(const YAML::Node& node, const char* key) const;
    [[nodiscard]] std::string require_string(const YAML::Node& node,
                                             const char* key,
                                             const char* context) const;
    [[nodiscard]]std::string resolve_env(const std::string& value)const ;
    // =====================================================================
    //                    HELPERS DE PARSING
    // =====================================================================
    std::chrono::seconds  parse_duration(const std::string& s) const;
    std::uint64_t parse_size(const std::string& s) const;
    // convertit un YAML scalar en SeedValue
    [[nodiscard]] sea::domain::SeedValue
    yaml_node_to_seed_value(const YAML::Node& node) const;
    // Helpers de parsing de la section logging
    sea::domain::logging::RotationConfig
    parse_rotation_node(const YAML::Node& node) const;

    sea::domain::logging::SinkConfig
    parse_sink_node(const YAML::Node& node) const;

    sea::domain::logging::AsyncConfig
    parse_async_node(const YAML::Node& node) const;

    sea::domain::logging::LoggingConfig
    parse_logging_node(const YAML::Node& node) const;

};

} // namespace sea::infrastructure::yaml