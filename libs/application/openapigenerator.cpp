#include "openapigenerator.h"

#include <cctype>
#include <functional>
#include <sstream>
#include <string>

#include "schema.h"
#include "security_scheme/security_config.h"
#include "security_scheme/authentification_config.h"

// ✨ Includes pour access_control
#include "access_control/crud_operation.h"
#include "access_control/access_control_spec.h"
#include "access_control/policy_condition.h"
#include "access_control/policy_predicate.h"
#include "access_control/policy_value_ref.h"

namespace sea::application {

OpenApiGenerator::OpenApiGenerator() {}

// =====================================================================
//                       MÉTHODE PUBLIQUE PRINCIPALE
// =====================================================================

nlohmann::json OpenApiGenerator::generate(
    const domain::Service& service,
    const std::vector<RouteDefinition>& route_definitions
    ) const {
    using json = nlohmann::json;

    json doc;

    // --- Info générale ---
    doc["openapi"] = "3.0.3";
    doc["info"] = {
        {"title", service.name + " API"},
        {"version", "1.0.0"},
        {"description", "Documentation OpenAPI generee automatiquement par SeaDesktop"}
    };

    doc["servers"] = json::array({
        {{"url", "http://localhost:" + std::to_string(service.port)}}
    });

    // --- Sécurité globale (deny by default si auth activée) ---
    const bool auth_enabled = service_has_auth(service);

    if (auth_enabled) {
        doc["security"] = json::array({bearer_security()});
    }

    // --- Components ---
    doc["paths"] = json::object();
    doc["components"] = {
        {"schemas", json::object()}
    };

    doc["components"]["securitySchemes"] = {
        {"bearerAuth", {
                           {"type", "http"},
                           {"scheme", "bearer"},
                           {"bearerFormat", "JWT"},
                           {"description", "JWT token obtenu via /auth/login"}
                       }}
    };

    // --- Schémas des entités ---
    for (const auto& entity : service.schema.entities) {
        doc["components"]["schemas"][entity.name] = make_entity_schema(entity);
        doc["components"]["schemas"][entity.name + "Input"] = make_entity_input_schema(entity);
    }

    // --- Schémas auth ---
    if (auth_enabled) {
        add_auth_schemas(doc["components"]["schemas"]);
    }

    // --- Paths CRUD ---
    for (const auto& route : route_definitions) {
        add_crud_path(doc["paths"], route, service);
    }

    // --- Paths relations ---
    add_relation_paths(doc["paths"], service);

    // --- Paths auth ---
    if (auth_enabled) {
        add_auth_paths(doc["paths"]);
    }

    // --- Health ---
    add_health_path(doc["paths"]);

    return doc;
}

// =====================================================================
//                       HELPERS DE SÉCURITÉ
// =====================================================================

bool OpenApiGenerator::service_has_auth(const domain::Service& service) const {
    return service.security.authentication().type()
    != sea::domain::security::AuthType::None;
}

bool OpenApiGenerator::schema_has_auth_source(const domain::Schema& schema) const {
    for (const auto& entity : schema.entities) {
        if (entity.options.is_auth_source) {
            return true;
        }
    }
    return false;
}

OpenApiGenerator::json OpenApiGenerator::bearer_security() const {
    return {{"bearerAuth", json::array()}};
}

// =====================================================================
//                       SCHÉMAS ENTITÉS
// =====================================================================

OpenApiGenerator::json OpenApiGenerator::make_entity_schema(
    const domain::Entity& entity
    ) const {
    json properties = json::object();
    json required = json::array();

    for (const auto& field : entity.fields) {
        if (!field.serializable) {
            continue;
        }

        properties[field.name] = field_to_openapi_schema(field);

        if (field.required) {
            required.push_back(field.name);
        }
    }

    json schema = {
        {"type", "object"},
        {"properties", properties}
    };

    if (!required.empty()) {
        schema["required"] = required;
    }

    return schema;
}

OpenApiGenerator::json OpenApiGenerator::make_entity_input_schema(
    const domain::Entity& entity
    ) const {
    json properties = json::object();
    json required = json::array();

    for (const auto& field : entity.fields) {
        if (field.name == "id") continue;
        if (!field.serializable && field.name != "password") continue;

        properties[field.name] = field_to_openapi_schema(field);

        if (field.required) {
            required.push_back(field.name);
        }
    }

    json schema = {
        {"type", "object"},
        {"properties", properties}
    };

    if (!required.empty()) {
        schema["required"] = required;
    }

    return schema;
}

OpenApiGenerator::json OpenApiGenerator::field_to_openapi_schema(
    const domain::Field& field
    ) const {
    json schema = json::object();

    using sea::domain::FieldType;

    switch (field.type) {
    case FieldType::String:
    case FieldType::Text:
    case FieldType::Password:
    // case FieldType::Url:
    //     schema["type"] = "string";
    //     break;

    case FieldType::Email:
        schema["type"] = "string";
        schema["format"] = "email";
        break;

    case FieldType::UUID:
        schema["type"] = "string";
        schema["format"] = "uuid";
        break;

    case FieldType::Int:
        schema["type"] = "integer";
        break;

    case FieldType::Float:
        schema["type"] = "number";
        break;

    case FieldType::Bool:
        schema["type"] = "boolean";
        break;

    // case FieldType::Date:
    //     schema["type"] = "string";
    //     schema["format"] = "date";
    //     break;

    // case FieldType::DateTime:
    // case FieldType::Timestamp:
    //     schema["type"] = "string";
    //     schema["format"] = "date-time";
    //     break;

    // case FieldType::Json:
    //     schema["type"] = "object";
    //     break;

    default:
        schema["type"] = "string";
        break;
    }

    if (field.has_default()) {
        std::visit(
            [&schema](const auto& value) {
                using T = std::decay_t<decltype(value)>;

                if constexpr (std::is_same_v<T, std::monostate>) {
                    // Pas de default, ne rien faire
                }
                else if constexpr (std::is_same_v<T, std::string>) {
                    if (!value.empty()) {
                        schema["default"] = value;
                    }
                }
                else if constexpr (std::is_same_v<T, std::int64_t>) {
                    schema["default"] = value;
                }
                else if constexpr (std::is_same_v<T, double>) {
                    schema["default"] = value;
                }
                else if constexpr (std::is_same_v<T, bool>) {
                    schema["default"] = value;
                }
            },
            field.default_val
            );
    }

    if (field.max_length.has_value()) {
        schema["maxLength"] = *field.max_length;
    }

    return schema;
}

void OpenApiGenerator::add_auth_schemas(json& schemas) const {
    schemas["LoginRequest"] = {
        {"type", "object"},
        {"required", json::array({"email", "password"})},
        {"properties", {
                           {"email", {{"type", "string"}, {"format", "email"}}},
                           {"password", {{"type", "string"}}}
                       }}
    };

    schemas["RegisterRequest"] = {
        {"type", "object"},
        {"required", json::array({"email", "password"})},
        {"properties", {
                           {"email", {{"type", "string"}, {"format", "email"}}},
                           {"password", {{"type", "string"}}},
                           {"full_name", {{"type", "string"}}}
                       }}
    };

    schemas["TokenResponse"] = {
        {"type", "object"},
        {"properties", {
                           {"access_token", {{"type", "string"}}},
                           {"refresh_token", {{"type", "string"}}},
                           {"token_type", {{"type", "string"}, {"example", "Bearer"}}},
                           {"user", {{"type", "object"}}}
                       }}
    };

    schemas["RefreshRequest"] = {
        {"type", "object"},
        {"required", json::array({"refresh_token"})},
        {"properties", {
                           {"refresh_token", {{"type", "string"}}}
                       }}
    };

    schemas["ErrorResponse"] = {
        {"type", "object"},
        {"properties", {
                           {"error", {{"type", "string"}}},
                           {"message", {{"type", "string"}}}
                       }}
    };
}

// =====================================================================
//                       PATHS CRUD
// =====================================================================

void OpenApiGenerator::add_crud_path(
    json& paths,
    const RouteDefinition& route,
    const domain::Service& service
    ) const {
    const auto http_method = to_openapi_method(route.method);
    if (http_method.empty()) {
        return;
    }

    if (route.operation_name != "list" &&
        route.operation_name != "create" &&
        route.operation_name != "get_by_id" &&
        route.operation_name != "update" &&
        route.operation_name != "delete") {
        return;
    }

    // Utilise directement le flag de RouteDefinition
    const bool requires_auth = route.requires_auth;

    std::string entity_plural = "/" + route.entity_name;
    entity_plural[1] = static_cast<char>(
        std::tolower(static_cast<unsigned char>(entity_plural[1]))
        );
    entity_plural += "s";

    const std::string item_path = entity_plural + "/{id}";

    if (route.operation_name == "list") {
        json op = {
            {"tags", json::array({route.entity_name})},
            {"summary", "List " + route.entity_name},
            {"responses", {
                              {"200", {
                                          {"description", "Liste des enregistrements"},
                                          {"content", {
                                                          {"application/json", {
                                                                                   {"schema", {
                                                                                                  {"type", "array"},
                                                                                                  {"items", {{"$ref", "#/components/schemas/" + route.entity_name}}}
                                                                                              }}
                                                                               }}
                                                      }}
                                      }},
                              {"401", {
                                          {"description", "Non authentifie"},
                                          {"content", {
                                                          {"application/json", {
                                                                                   {"schema", {{"$ref", "#/components/schemas/ErrorResponse"}}}
                                                                               }}
                                                      }}
                                      }}
                          }}
        };

        if (requires_auth) {
            op["security"] = json::array({bearer_security()});
        } else {
            op["security"] = json::array();  // public explicite
        }

        // Enrichissement avec access_control
        enrich_with_access_control(op, service, route.entity_name, route.operation_name);

        paths[entity_plural][http_method] = op;
    }
    else if (route.operation_name == "create") {
        json op = {
            {"tags", json::array({route.entity_name})},
            {"summary", "Create " + route.entity_name},
            {"requestBody", {
                                {"required", true},
                                {"content", {
                                                {"application/json", {
                                                                         {"schema", {{"$ref", "#/components/schemas/" + route.entity_name + "Input"}}}
                                                                     }}
                                            }}
                            }},
            {"responses", {
                              {"201", {
                                          {"description", "Enregistrement cree"},
                                          {"content", {
                                                          {"application/json", {
                                                                                   {"schema", {{"$ref", "#/components/schemas/" + route.entity_name}}}
                                                                               }}
                                                      }}
                                      }},
                              {"400", {
                                          {"description", "Requete invalide"},
                                          {"content", {
                                                          {"application/json", {
                                                                                   {"schema", {{"$ref", "#/components/schemas/ErrorResponse"}}}
                                                                               }}
                                                      }}
                                      }},
                              {"401", {{"description", "Non authentifie"}}}
                          }}
        };

        if (requires_auth) {
            op["security"] = json::array({bearer_security()});
        } else {
            op["security"] = json::array();
        }

        // ✨ Enrichissement avec access_control
        enrich_with_access_control(op, service, route.entity_name, route.operation_name);

        paths[entity_plural][http_method] = op;
    }
    else if (route.operation_name == "get_by_id") {
        json op = {
            {"tags", json::array({route.entity_name})},
            {"summary", "Get " + route.entity_name + " by id"},
            {"parameters", json::array({
                               {
                                   {"name", "id"},
                                   {"in", "path"},
                                   {"required", true},
                                   {"schema", {{"type", "string"}}}
                               }
                           })},
            {"responses", {
                              {"200", {
                                          {"description", "Enregistrement trouve"},
                                          {"content", {
                                                          {"application/json", {
                                                                                   {"schema", {{"$ref", "#/components/schemas/" + route.entity_name}}}
                                                                               }}
                                                      }}
                                      }},
                              {"401", {{"description", "Non authentifie"}}},
                              {"404", {{"description", "Introuvable"}}}
                          }}
        };

        if (requires_auth) {
            op["security"] = json::array({bearer_security()});
        } else {
            op["security"] = json::array();
        }

        // ✨ Enrichissement avec access_control
        enrich_with_access_control(op, service, route.entity_name, route.operation_name);

        paths[item_path][http_method] = op;
    }
    else if (route.operation_name == "update") {
        json op = {
            {"tags", json::array({route.entity_name})},
            {"summary", "Update " + route.entity_name},
            {"parameters", json::array({
                               {
                                   {"name", "id"},
                                   {"in", "path"},
                                   {"required", true},
                                   {"schema", {{"type", "string"}}}
                               }
                           })},
            {"requestBody", {
                                {"required", true},
                                {"content", {
                                                {"application/json", {
                                                                         {"schema", {{"$ref", "#/components/schemas/" + route.entity_name + "Input"}}}
                                                                     }}
                                            }}
                            }},
            {"responses", {
                              {"200", {
                                          {"description", "Enregistrement mis a jour"},
                                          {"content", {
                                                          {"application/json", {
                                                                                   {"schema", {{"$ref", "#/components/schemas/" + route.entity_name}}}
                                                                               }}
                                                      }}
                                      }},
                              {"400", {{"description", "Requete invalide"}}},
                              {"401", {{"description", "Non authentifie"}}},
                              {"404", {{"description", "Introuvable"}}}
                          }}
        };

        if (requires_auth) {
            op["security"] = json::array({bearer_security()});
        } else {
            op["security"] = json::array();
        }

        // ✨ Enrichissement avec access_control
        enrich_with_access_control(op, service, route.entity_name, route.operation_name);

        paths[item_path][http_method] = op;
    }
    else if (route.operation_name == "delete") {
        json op = {
            {"tags", json::array({route.entity_name})},
            {"summary", "Delete " + route.entity_name},
            {"parameters", json::array({
                               {
                                   {"name", "id"},
                                   {"in", "path"},
                                   {"required", true},
                                   {"schema", {{"type", "string"}}}
                               }
                           })},
            {"responses", {
                              {"200", {{"description", "Supprime"}}},
                              {"401", {{"description", "Non authentifie"}}},
                              {"404", {{"description", "Introuvable"}}}
                          }}
        };

        if (requires_auth) {
            op["security"] = json::array({bearer_security()});
        } else {
            op["security"] = json::array();
        }

        // ✨ Enrichissement avec access_control
        enrich_with_access_control(op, service, route.entity_name, route.operation_name);

        paths[item_path][http_method] = op;
    }
}

// =====================================================================
//                       PATHS RELATIONNELS
// =====================================================================

void OpenApiGenerator::add_relation_paths(
    json& paths,
    const domain::Service& service
    ) const {
    const bool requires_auth = service_has_auth(service);

    for (const auto& entity : service.schema.entities) {
        // Plural path de l'entité parent
        std::string parent_plural = "/" + entity.name;
        parent_plural[1] = static_cast<char>(
            std::tolower(static_cast<unsigned char>(parent_plural[1]))
            );
        parent_plural += "s";

        std::string parent_lower = entity.name;
        parent_lower[0] = static_cast<char>(
            std::tolower(static_cast<unsigned char>(parent_lower[0]))
            );

        for (const auto& relation : entity.relations) {
            using sea::domain::RelationKind;

            if (relation.kind == RelationKind::HasMany) {
                std::string child_plural = "/" + relation.target_entity;
                child_plural[1] = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(child_plural[1]))
                    );
                child_plural += "s";

                // /children/filter/with_parent/{id}
                const std::string by_id_path = child_plural + "/filter/with_" + parent_lower + "/{id}";

                json by_id_op = {
                    {"tags", json::array({relation.target_entity})},
                    {"summary", "List " + relation.target_entity + " by " + entity.name + " id"},
                    {"parameters", json::array({
                                       {
                                           {"name", "id"},
                                           {"in", "path"},
                                           {"required", true},
                                           {"schema", {{"type", "string"}}}
                                       }
                                   })},
                    {"responses", {
                                      {"200", {
                                                  {"description", "Liste filtree"},
                                                  {"content", {
                                                                  {"application/json", {
                                                                                           {"schema", {
                                                                                                          {"type", "array"},
                                                                                                          {"items", {{"$ref", "#/components/schemas/" + relation.target_entity}}}
                                                                                                      }}
                                                                                       }}
                                                              }}
                                              }},
                                      {"401", {{"description", "Non authentifie"}}}
                                  }}
                };

                if (requires_auth) {
                    by_id_op["security"] = json::array({bearer_security()});
                } else {
                    by_id_op["security"] = json::array();
                }

                paths[by_id_path]["get"] = by_id_op;

                // /children/filter/with_parent_<unique_field>/{value}
                for (const auto& field : entity.fields) {
                    if (!field.unique || field.name == "id") {
                        continue;
                    }

                    const std::string by_field_path =
                        child_plural + "/filter/with_" + parent_lower + "_" + field.name + "/{value}";

                    json by_field_op = {
                        {"tags", json::array({relation.target_entity})},
                        {"summary", "List " + relation.target_entity + " by " + entity.name + " " + field.name},
                        {"parameters", json::array({
                                           {
                                               {"name", "value"},
                                               {"in", "path"},
                                               {"required", true},
                                               {"schema", {{"type", "string"}}}
                                           }
                                       })},
                        {"responses", {
                                          {"200", {
                                                      {"description", "Liste filtree"},
                                                      {"content", {
                                                                      {"application/json", {
                                                                                               {"schema", {
                                                                                                              {"type", "array"},
                                                                                                              {"items", {{"$ref", "#/components/schemas/" + relation.target_entity}}}
                                                                                                          }}
                                                                                           }}
                                                                  }}
                                                  }},
                                          {"401", {{"description", "Non authentifie"}}},
                                          {"404", {{"description", "Introuvable"}}}
                                      }}
                    };

                    if (requires_auth) {
                        by_field_op["security"] = json::array({bearer_security()});
                    } else {
                        by_field_op["security"] = json::array();
                    }

                    paths[by_field_path]["get"] = by_field_op;
                }

                // /parents_with_relation/{id}
                const std::string with_children_path = parent_plural + "_with_" + relation.name + "/{id}";

                json with_children_op = {
                    {"tags", json::array({entity.name})},
                    {"summary", "Get " + entity.name + " with " + relation.name},
                    {"parameters", json::array({
                                       {
                                           {"name", "id"},
                                           {"in", "path"},
                                           {"required", true},
                                           {"schema", {{"type", "string"}}}
                                       }
                                   })},
                    {"responses", {
                                      {"200", {
                                                  {"description", entity.name + " avec ses " + relation.name}
                                              }},
                                      {"401", {{"description", "Non authentifie"}}},
                                      {"404", {{"description", "Introuvable"}}}
                                  }}
                };

                if (requires_auth) {
                    with_children_op["security"] = json::array({bearer_security()});
                } else {
                    with_children_op["security"] = json::array();
                }

                paths[with_children_path]["get"] = with_children_op;
            }

            if (relation.kind == RelationKind::HasOne) {
                const std::string path = parent_plural + "/" + relation.name + "/{id}";

                json op = {
                    {"tags", json::array({relation.target_entity})},
                    {"summary", "Get " + relation.name + " of " + entity.name},
                    {"parameters", json::array({
                                       {
                                           {"name", "id"},
                                           {"in", "path"},
                                           {"required", true},
                                           {"schema", {{"type", "string"}}}
                                       }
                                   })},
                    {"responses", {
                                      {"200", {
                                                  {"description", "Enregistrement trouve"},
                                                  {"content", {
                                                                  {"application/json", {
                                                                                           {"schema", {{"$ref", "#/components/schemas/" + relation.target_entity}}}
                                                                                       }}
                                                              }}
                                              }},
                                      {"401", {{"description", "Non authentifie"}}},
                                      {"404", {{"description", "Introuvable"}}}
                                  }}
                };

                if (requires_auth) {
                    op["security"] = json::array({bearer_security()});
                } else {
                    op["security"] = json::array();
                }

                paths[path]["get"] = op;
            }

            if (relation.kind == RelationKind::ManyToMany) {
                std::string target_plural = "/" + relation.target_entity;
                target_plural[1] = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(target_plural[1]))
                    );
                target_plural += "s";

                const std::string m2m_path = target_plural + "/filter/with_" + parent_lower + "/{id}";

                json op = {
                    {"tags", json::array({relation.target_entity})},
                    {"summary", "List " + relation.target_entity + " of " + entity.name},
                    {"parameters", json::array({
                                       {
                                           {"name", "id"},
                                           {"in", "path"},
                                           {"required", true},
                                           {"schema", {{"type", "string"}}}
                                       }
                                   })},
                    {"responses", {
                                      {"200", {
                                                  {"description", "Liste"},
                                                  {"content", {
                                                                  {"application/json", {
                                                                                           {"schema", {
                                                                                                          {"type", "array"},
                                                                                                          {"items", {{"$ref", "#/components/schemas/" + relation.target_entity}}}
                                                                                                      }}
                                                                                       }}
                                                              }}
                                              }},
                                      {"401", {{"description", "Non authentifie"}}}
                                  }}
                };

                if (requires_auth) {
                    op["security"] = json::array({bearer_security()});
                } else {
                    op["security"] = json::array();
                }

                paths[m2m_path]["get"] = op;
            }
        }
    }
}

// =====================================================================
//                       PATHS AUTH
// =====================================================================

void OpenApiGenerator::add_auth_paths(json& paths) const {
    // POST /auth/register (public)
    paths["/auth/register"]["post"] = {
        {"tags", json::array({"Auth"})},
        {"summary", "Inscription d'un nouvel utilisateur"},
        {"security", json::array()},
        {"requestBody", {
                            {"required", true},
                            {"content", {
                                            {"application/json", {
                                                                     {"schema", {{"$ref", "#/components/schemas/RegisterRequest"}}}
                                                                 }}
                                        }}
                        }},
        {"responses", {
                          {"201", {
                                      {"description", "Utilisateur cree"},
                                      {"content", {
                                                      {"application/json", {
                                                                               {"schema", {{"$ref", "#/components/schemas/TokenResponse"}}}
                                                                           }}
                                                  }}
                                  }},
                          {"400", {{"description", "Requete invalide"}}},
                          {"409", {{"description", "Email deja utilise"}}}
                      }}
    };

    // POST /auth/login (public)
    paths["/auth/login"]["post"] = {
        {"tags", json::array({"Auth"})},
        {"summary", "Connexion utilisateur"},
        {"security", json::array()},
        {"requestBody", {
                            {"required", true},
                            {"content", {
                                            {"application/json", {
                                                                     {"schema", {{"$ref", "#/components/schemas/LoginRequest"}}}
                                                                 }}
                                        }}
                        }},
        {"responses", {
                          {"200", {
                                      {"description", "Authentification reussie"},
                                      {"content", {
                                                      {"application/json", {
                                                                               {"schema", {{"$ref", "#/components/schemas/TokenResponse"}}}
                                                                           }}
                                                  }}
                                  }},
                          {"401", {{"description", "Identifiants invalides"}}}
                      }}
    };

    // POST /auth/refresh (public, mais utilise refresh_token)
    paths["/auth/refresh"]["post"] = {
        {"tags", json::array({"Auth"})},
        {"summary", "Rafraichir le token d'acces"},
        {"security", json::array()},
        {"requestBody", {
                            {"required", true},
                            {"content", {
                                            {"application/json", {
                                                                     {"schema", {{"$ref", "#/components/schemas/RefreshRequest"}}}
                                                                 }}
                                        }}
                        }},
        {"responses", {
                          {"200", {
                                      {"description", "Token rafraichi"},
                                      {"content", {
                                                      {"application/json", {
                                                                               {"schema", {{"$ref", "#/components/schemas/TokenResponse"}}}
                                                                           }}
                                                  }}
                                  }},
                          {"401", {{"description", "Refresh token invalide"}}}
                      }}
    };

    // GET /auth/me (protégée)
    paths["/auth/me"]["get"] = {
        {"tags", json::array({"Auth"})},
        {"summary", "Profil utilisateur courant"},
        {"security", json::array({bearer_security()})},
        {"responses", {
                          {"200", {{"description", "Profil utilisateur"}}},
                          {"401", {{"description", "Non authentifie"}}}
                      }}
    };

    // POST /auth/logout (protégée)
    paths["/auth/logout"]["post"] = {
        {"tags", json::array({"Auth"})},
        {"summary", "Deconnexion"},
        {"security", json::array({bearer_security()})},
        {"responses", {
                          {"200", {{"description", "Deconnexion reussie"}}},
                          {"401", {{"description", "Non authentifie"}}}
                      }}
    };
}

// =====================================================================
//                       PATH HEALTH
// =====================================================================

void OpenApiGenerator::add_health_path(json& paths) const {
    paths["/health"]["get"] = {
        {"tags", json::array({"System"})},
        {"summary", "Health check"},
        {"security", json::array()},
        {"responses", {
                          {"200", {
                                      {"description", "Service en bonne sante"},
                                      {"content", {
                                                      {"application/json", {
                                                                               {"schema", {
                                                                                              {"type", "object"},
                                                                                              {"properties", {
                                                                                                                 {"status", {{"type", "string"}, {"example", "ok"}}}
                                                                                                             }}
                                                                                          }}
                                                                           }}
                                                  }}
                                  }}
                      }}
    };
}

// =====================================================================
//                       CONVERSION HTTP METHOD
// =====================================================================

std::string OpenApiGenerator::to_openapi_method(HttpMethod method) const {
    switch (method) {
    case HttpMethod::Get:    return "get";
    case HttpMethod::Post:   return "post";
    case HttpMethod::Put:    return "put";
    case HttpMethod::Delete: return "delete";
    }
    return "";
}

// =====================================================================
// ✨ HELPERS ACCESS CONTROL (RBAC + ABAC)
// =====================================================================

void OpenApiGenerator::enrich_with_access_control(
    json& op,
    const domain::Service& service,
    const std::string& entity_name,
    const std::string& operation_name) const
{
    // Si l'autorisation n'est pas activée globalement, rien à enrichir
    if (!service.access_control.enabled()) {
        return;
    }

    // Sur les routes protégées, ajouter systématiquement la réponse 403
    if (op.contains("security") && !op["security"].empty()) {
        op["responses"]["403"] = {
            {"description", "Forbidden - Permissions insuffisantes pour cette operation"},
            {"content", {
                            {"application/json", {
                                                     {"schema", {{"$ref", "#/components/schemas/ErrorResponse"}}}
                                                 }}
                        }}
        };
    }

    // Trouve l'entité pour lire ses règles d'access_control
    const auto* entity = find_entity_by_name(service, entity_name);
    if (entity == nullptr) {
        return;
    }

    // Construit la description Markdown des règles
    const auto auth_desc = build_authorization_description(*entity, operation_name);
    if (auth_desc.empty()) {
        return;
    }

    // Ajoute (ou enrichit) la description
    std::string current_desc = op.value("description", "");
    if (!current_desc.empty()) {
        current_desc += "\n\n";
    }
    op["description"] = current_desc + auth_desc;
}

const sea::domain::Entity* OpenApiGenerator::find_entity_by_name(
    const domain::Service& service,
    const std::string& entity_name) const
{
    for (const auto& entity : service.schema.entities) {
        if (entity.name == entity_name) {
            return &entity;
        }
    }
    return nullptr;
}

std::string OpenApiGenerator::build_authorization_description(
    const domain::Entity& entity,
    const std::string& operation_name) const
{
    using namespace sea::domain::access_control;

    // Convertit "list" / "get_by_id" / "create" / "update" / "delete" en CrudOperation
    const auto op_opt = crud_operation_from_string(operation_name);
    if (!op_opt.has_value()) {
        return "";
    }

    // Récupère la spec d'access_control pour cette opération
    const auto* spec = entity.access_control.find_spec(*op_opt);
    if (spec == nullptr || spec->is_empty()) {
        return "";
    }

    std::ostringstream oss;
    oss << "### Access Control\n\n";

    // Stratégie d'évaluation : fast path (subject-only) ou slow path (resource-aware)
    if (spec->requires_resource()) {
        oss << "**Strategie**: Resource-aware (after DB load) - slow path\n\n";
    } else {
        oss << "**Strategie**: Subject-only - fast path\n\n";
    }

    // Helper pour formatter une référence (subject.x, resource.x, literal, etc.)
    auto format_ref = [](const PolicyValueRef& ref) -> std::string {
        switch (ref.source) {
        case PolicyValueSource::Subject:
            return "subject." + ref.path;
        case PolicyValueSource::Resource:
            return "resource." + ref.path;
        case PolicyValueSource::Context:
            return "context." + ref.path;
        case PolicyValueSource::Literal:
            if (!ref.literal_list.empty()) {
                std::string s = "[";
                for (std::size_t i = 0; i < ref.literal_list.size(); ++i) {
                    if (i > 0) s += ", ";
                    s += ref.literal_list[i];
                }
                s += "]";
                return s;
            }
            return "'" + ref.literal + "'";
        }
        return "?";
    };

    // Helper pour décrire un prédicat
    auto describe_predicate = [&](const PolicyPredicate& pred) -> std::string {
        return format_ref(pred.left) + " "
               + std::string(to_string(pred.op)) + " "
               + format_ref(pred.right);
    };

    // Helper récursif pour décrire une condition (avec indentation)
    std::function<void(const PolicyCondition&, const std::string&)> describe_condition;
    describe_condition = [&](const PolicyCondition& cond, const std::string& indent) {
        switch (cond.type()) {
        case PolicyConditionType::Predicate:
            if (cond.predicate().has_value()) {
                oss << indent << "- `"
                    << describe_predicate(*cond.predicate()) << "`\n";
            }
            break;

        case PolicyConditionType::All:
            oss << indent << "- **AND** (toutes les conditions) :\n";
            for (const auto& child : cond.children()) {
                describe_condition(child, indent + "    ");
            }
            break;

        case PolicyConditionType::Any:
            oss << indent << "- **OR** (au moins une condition) :\n";
            for (const auto& child : cond.children()) {
                describe_condition(child, indent + "    ");
            }
            break;

        case PolicyConditionType::Not:
            oss << indent << "- **NOT** :\n";
            if (!cond.children().empty()) {
                describe_condition(cond.children()[0], indent + "    ");
            }
            break;
        }
    };

    oss << "**Regles** :\n\n";
    describe_condition(spec->condition(), "");

    return oss.str();
}

} // namespace sea::application