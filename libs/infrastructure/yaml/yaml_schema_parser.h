#pragma once

#include <string>

#include <yaml-cpp/yaml.h>

#include "project.h"
#include "service.h"
#include "entity.h"
#include "field.h"
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

    [[nodiscard]] sea::domain::Entity parse_entity_node(const YAML::Node& node) const;
    [[nodiscard]] sea::domain::Field parse_field_node(const YAML::Node& node) const;
    [[nodiscard]] sea::domain::Relation parse_relation_node(const YAML::Node& node) const;
    [[nodiscard]] sea::domain::DatabaseConfig parse_database_config_node(const YAML::Node& node) const;

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
};

} // namespace sea::infrastructure::yaml
