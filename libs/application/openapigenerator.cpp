#include "openapigenerator.h"

#include <cctype>
#include <string>

#include "schema.h"
#include "security_scheme/security_config.h"
#include "security_scheme/authentification_config.h"

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
                           {"description", "JWT obtenu via /auth/login ou /auth/register. Format: Bearer <token>"}
                       }}
    };

    // Schémas d'entités
    for (const auto& entity : service.schema.entities) {
        doc["components"]["schemas"][entity.name] = make_entity_schema(entity);
        doc["components"]["schemas"][entity.name + "Input"] = make_entity_input_schema(entity);
    }

    // Schémas d'authentification (si on a une auth source)
    const bool has_auth_source = schema_has_auth_source(service.schema);
    if (has_auth_source) {
        add_auth_schemas(doc["components"]["schemas"]);
    }

    // --- Paths ---

    // Routes CRUD générées depuis les RouteDefinition
    for (const auto& route : route_definitions) {
        add_crud_path(doc["paths"], route, service);
    }

    // Routes relationnelles
    add_relation_paths(doc["paths"], service);

    // Routes d'authentification (si une entité est auth source)
    if (has_auth_source) {
        add_auth_paths(doc["paths"]);
    }

    // Health check (toujours public)
    add_health_path(doc["paths"]);

    return doc;
}

// =====================================================================
//                          HELPERS DE SÉCURITÉ
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
    return json{{"bearerAuth", json::array()}};
}

// =====================================================================
//                          SCHÉMAS D'ENTITÉS
// =====================================================================

OpenApiGenerator::json OpenApiGenerator::make_entity_schema(
    const domain::Entity& entity
    ) const {
    json schema;
    schema["type"] = "object";
    schema["properties"] = json::object();

    json required = json::array();

    for (const auto& field : entity.fields) {
        if (!field.serializable) {
            continue;
        }

        schema["properties"][field.name] = field_to_openapi_schema(field);

        if (field.required) {
            required.push_back(field.name);
        }
    }

    if (!required.empty()) {
        schema["required"] = required;
    }

    return schema;
}

OpenApiGenerator::json OpenApiGenerator::make_entity_input_schema(
    const domain::Entity& entity
    ) const {
    json schema;
    schema["type"] = "object";
    schema["properties"] = json::object();

    json required = json::array();

    for (const auto& field : entity.fields) {
        if (field.name == "id") {
            continue;
        }

        schema["properties"][field.name] = field_to_openapi_schema(field);

        if (field.required) {
            required.push_back(field.name);
        }
    }

    if (!required.empty()) {
        schema["required"] = required;
    }

    return schema;
}

OpenApiGenerator::json OpenApiGenerator::field_to_openapi_schema(
    const domain::Field& field
    ) const {
    switch (field.type) {
    case sea::domain::FieldType::Int:
        return json{{"type", "integer"}, {"format", "int64"}};

    case sea::domain::FieldType::Float:
        return json{{"type", "number"}, {"format", "double"}};

    case sea::domain::FieldType::Bool:
        return json{{"type", "boolean"}};

    case sea::domain::FieldType::Email:
        return json{{"type", "string"}, {"format", "email"}};

    case sea::domain::FieldType::Password:
        return json{{"type", "string"}, {"format", "password"}};

    case sea::domain::FieldType::UUID:
        return json{{"type", "string"}, {"format", "uuid"}};

    case sea::domain::FieldType::String:
    default:
        return json{{"type", "string"}};
    }
}

// =====================================================================
//                       SCHÉMAS D'AUTHENTIFICATION
// =====================================================================

void OpenApiGenerator::add_auth_schemas(json& schemas) const {
    // LoginRequest
    schemas["LoginRequest"] = {
        {"type", "object"},
        {"required", json::array({"email", "password"})},
        {"properties", {
                           {"email", {{"type", "string"}, {"format", "email"}}},
                           {"password", {{"type", "string"}, {"format", "password"}}}
                       }}
    };

    // RegisterRequest
    schemas["RegisterRequest"] = {
        {"type", "object"},
        {"required", json::array({"email", "password", "full_name"})},
        {"properties", {
                           {"email", {{"type", "string"}, {"format", "email"}}},
                           {"password", {{"type", "string"}, {"format", "password"}, {"minLength", 8}}},
                           {"full_name", {{"type", "string"}}},
                           {"role", {{"type", "string"}, {"example", "user"}}}
                       }}
    };

    // RefreshRequest
    schemas["RefreshRequest"] = {
        {"type", "object"},
        {"required", json::array({"refresh_token"})},
        {"properties", {
                           {"refresh_token", {{"type", "string"}}}
                       }}
    };

    // AuthTokens (réponse de login/register/refresh)
    schemas["AuthTokens"] = {
        {"type", "object"},
        {"properties", {
                           {"access_token", {
                                                {"type", "string"},
                                                {"description", "JWT a utiliser dans le header Authorization (TTL ~15min)"}
                                            }},
                           {"refresh_token", {
                                                 {"type", "string"},
                                                 {"description", "Token longue durée pour obtenir de nouveaux access_token"}
                                             }},
                           {"token_type", {{"type", "string"}, {"example", "Bearer"}}},
                           {"expires_in", {
                                              {"type", "integer"},
                                              {"description", "Secondes avant expiration de l'access_token"}
                                          }}
                       }}
    };

    // ErrorResponse
    schemas["ErrorResponse"] = {
        {"type", "object"},
        {"properties", {
                           {"error", {{"type", "string"}}},
                           {"message", {{"type", "string"}}},
                           {"code", {{"type", "integer"}}}
                       }}
    };
}

// =====================================================================
//                          PATHS CRUD
// =====================================================================

void OpenApiGenerator::add_crud_path(
    json& paths,
    const RouteDefinition& route,
    const domain::Service& /*service*/
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
    // Détermine une fois si l'auth est activée au niveau service
    const bool requires_auth = service_has_auth(service);

    for (const auto& entity : service.schema.entities) {
        std::string parent_path = "/" + entity.name;
        parent_path[1] = static_cast<char>(
            std::tolower(static_cast<unsigned char>(parent_path[1]))
            );
        parent_path += "s";

        for (const auto& relation : entity.relations) {
            if (relation.kind == sea::domain::RelationKind::HasMany) {
                std::string child_path = "/" + relation.target_entity;
                child_path[1] = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(child_path[1]))
                    );
                child_path += "s";

                std::string parent_name = entity.name;
                parent_name[0] = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(parent_name[0]))
                    );

                // /children/filter/with_parent/{id}
                const std::string by_id_path =
                    child_path + "/filter/with_" + parent_name + "/{id}";

                json by_id_op = {
                    {"tags", json::array({entity.name})},
                    {"summary", "List related " + relation.target_entity + " by parent id"},
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
                                                  {"description", "Liste des enregistrements lies"},
                                                  {"content", {
                                                                  {"application/json", {
                                                                                           {"schema", {
                                                                                                          {"type", "array"},
                                                                                                          {"items", {{"$ref", "#/components/schemas/" + relation.target_entity}}}
                                                                                                      }}
                                                                                       }}
                                                              }}
                                              }},
                                      {"400", {{"description", "Parametre invalide"}}},
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
                        child_path + "/filter/with_" + parent_name + "_" + field.name + "/{value}";

                    json by_field_op = {
                        {"tags", json::array({entity.name})},
                        {"summary", "List related " + relation.target_entity + " by parent " + field.name},
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
                                                      {"description", "Liste des enregistrements lies"},
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
                                          {"404", {{"description", "Parent introuvable"}}}
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
                const std::string with_children_path =
                    parent_path + "_with_" + relation.name + "/{id}";

                json with_children_op = {
                    {"tags", json::array({entity.name})},
                    {"summary", "Get " + entity.name + " with related " + relation.name},
                    {"parameters", json::array({
                                       {
                                           {"name", "id"},
                                           {"in", "path"},
                                           {"required", true},
                                           {"schema", {{"type", "string"}}}
                                       }
                                   })},
                    {"responses", {
                                      {"200", {{"description", "Parent avec ses enfants"}}},
                                      {"401", {{"description", "Non authentifie"}}},
                                      {"404", {{"description", "Parent introuvable"}}}
                                  }}
                };

                if (requires_auth) {
                    with_children_op["security"] = json::array({bearer_security()});
                } else {
                    with_children_op["security"] = json::array();
                }

                paths[with_children_path]["get"] = with_children_op;
            }

            if (relation.kind == sea::domain::RelationKind::HasOne) {
                const std::string relation_path =
                    parent_path + "/" + relation.name + "/{id}";

                json op = {
                    {"tags", json::array({entity.name})},
                    {"summary", "Get related " + relation.target_entity},
                    {"parameters", json::array({
                                       {
                                           {"name", "id"},
                                           {"in", "path"},
                                           {"required", true},
                                           {"schema", {{"type", "string"}}}
                                       }
                                   })},
                    {"responses", {
                                      {"200", {{"description", "OK"}}},
                                      {"401", {{"description", "Non authentifie"}}},
                                      {"404", {{"description", "Introuvable"}}}
                                  }}
                };

                if (requires_auth) {
                    op["security"] = json::array({bearer_security()});
                } else {
                    op["security"] = json::array();
                }

                paths[relation_path]["get"] = op;
            }

            if (relation.kind == sea::domain::RelationKind::ManyToMany) {
                std::string target_path = "/" + relation.target_entity;
                target_path[1] = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(target_path[1]))
                    );
                target_path += "s";

                std::string source_name = entity.name;
                source_name[0] = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(source_name[0]))
                    );

                const std::string by_id_path =
                    target_path + "/filter/with_" + source_name + "/{id}";

                json op = {
                    {"tags", json::array({entity.name})},
                    {"summary", "List related " + relation.target_entity + " by many-to-many relation"},
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
                                                  {"description", "Liste des enregistrements lies"},
                                                  {"content", {
                                                                  {"application/json", {
                                                                                           {"schema", {
                                                                                                          {"type", "array"},
                                                                                                          {"items", {{"$ref", "#/components/schemas/" + relation.target_entity}}}
                                                                                                      }}
                                                                                       }}
                                                              }}
                                              }},
                                      {"400", {{"description", "Parametre invalide"}}},
                                      {"401", {{"description", "Non authentifie"}}}
                                  }}
                };

                if (requires_auth) {
                    op["security"] = json::array({bearer_security()});
                } else {
                    op["security"] = json::array();
                }

                paths[by_id_path]["get"] = op;
            }
        }
    }
}

// =====================================================================
//                    PATHS D'AUTHENTIFICATION
// =====================================================================

void OpenApiGenerator::add_auth_paths(json& paths) const {
    // POST /auth/register
    paths["/auth/register"]["post"] = {
        {"tags", json::array({"Authentication"})},
        {"summary", "Register a new user"},
        {"description", "Cree un nouveau compte utilisateur et retourne les tokens d'acces."},
        {"security", json::array()},   // public
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
                                      {"description", "Utilisateur cree avec succes"},
                                      {"content", {
                                                      {"application/json", {
                                                                               {"schema", {{"$ref", "#/components/schemas/AuthTokens"}}}
                                                                           }}
                                                  }}
                                  }},
                          {"400", {
                                      {"description", "Donnees invalides"},
                                      {"content", {
                                                      {"application/json", {
                                                                               {"schema", {{"$ref", "#/components/schemas/ErrorResponse"}}}
                                                                           }}
                                                  }}
                                  }},
                          {"409", {
                                      {"description", "Email deja utilise"},
                                      {"content", {
                                                      {"application/json", {
                                                                               {"schema", {{"$ref", "#/components/schemas/ErrorResponse"}}}
                                                                           }}
                                                  }}
                                  }}
                      }}
    };

    // POST /auth/login
    paths["/auth/login"]["post"] = {
        {"tags", json::array({"Authentication"})},
        {"summary", "Login with email and password"},
        {"description", "Verifie les credentials et retourne les tokens d'acces."},
        {"security", json::array()},   // public
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
                                      {"description", "Connexion reussie"},
                                      {"content", {
                                                      {"application/json", {
                                                                               {"schema", {{"$ref", "#/components/schemas/AuthTokens"}}}
                                                                           }}
                                                  }}
                                  }},
                          {"401", {
                                      {"description", "Identifiants invalides"},
                                      {"content", {
                                                      {"application/json", {
                                                                               {"schema", {{"$ref", "#/components/schemas/ErrorResponse"}}}
                                                                           }}
                                                  }}
                                  }}
                      }}
    };

    // POST /auth/refresh
    paths["/auth/refresh"]["post"] = {
        {"tags", json::array({"Authentication"})},
        {"summary", "Refresh access token"},
        {"description", "Echange un refresh_token valide contre un nouveau access_token. L'ancien refresh_token est invalide."},
        {"security", json::array()},   // public (utilise refresh_token, pas access_token)
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
                                      {"description", "Token rafraichi avec succes"},
                                      {"content", {
                                                      {"application/json", {
                                                                               {"schema", {{"$ref", "#/components/schemas/AuthTokens"}}}
                                                                           }}
                                                  }}
                                  }},
                          {"401", {
                                      {"description", "Refresh token invalide ou expire"},
                                      {"content", {
                                                      {"application/json", {
                                                                               {"schema", {{"$ref", "#/components/schemas/ErrorResponse"}}}
                                                                           }}
                                                  }}
                                  }}
                      }}
    };

    // POST /auth/logout
    paths["/auth/logout"]["post"] = {
        {"tags", json::array({"Authentication"})},
        {"summary", "Logout (revoke refresh token)"},
        {"description", "Invalide le refresh_token de la session courante."},
        {"security", json::array({bearer_security()})},
        {"responses", {
                          {"204", {{"description", "Deconnexion reussie"}}},
                          {"401", {
                                      {"description", "Token invalide ou expire"},
                                      {"content", {
                                                      {"application/json", {
                                                                               {"schema", {{"$ref", "#/components/schemas/ErrorResponse"}}}
                                                                           }}
                                                  }}
                                  }}
                      }}
    };

    // GET /auth/me
    paths["/auth/me"]["get"] = {
        {"tags", json::array({"Authentication"})},
        {"summary", "Get current authenticated user"},
        {"description", "Retourne l'utilisateur associe au JWT fourni."},
        {"security", json::array({bearer_security()})},
        {"responses", {
                          {"200", {
                                      {"description", "Utilisateur courant"},
                                      {"content", {
                                                      {"application/json", {
                                                                               {"schema", {{"$ref", "#/components/schemas/User"}}}
                                                                           }}
                                                  }}
                                  }},
                          {"401", {
                                      {"description", "Token invalide ou absent"},
                                      {"content", {
                                                      {"application/json", {
                                                                               {"schema", {{"$ref", "#/components/schemas/ErrorResponse"}}}
                                                                           }}
                                                  }}
                                  }}
                      }}
    };
}

// =====================================================================
//                          PATHS SYSTÈME
// =====================================================================

void OpenApiGenerator::add_health_path(json& paths) const {
    paths["/health"]["get"] = {
        {"tags", json::array({"System"})},
        {"summary", "Health check"},
        {"description", "Verifie que le service est en ligne."},
        {"security", json::array()},   // toujours public
        {"responses", {
                          {"200", {
                                      {"description", "Service en ligne"},
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
    case sea::application::HttpMethod::Get:    return "get";
    case sea::application::HttpMethod::Post:   return "post";
    case sea::application::HttpMethod::Put:    return "put";
    case sea::application::HttpMethod::Delete: return "delete";
    default: return "";
    }
}

} // namespace sea::application