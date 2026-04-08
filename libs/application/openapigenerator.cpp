#include "openapigenerator.h"

#include <cctype>
#include <string>
#include <vector>

#include "schema.h"

namespace sea::application {

OpenApiGenerator::OpenApiGenerator() {}

nlohmann::json OpenApiGenerator::generate(
    const domain::Service& service,
    const std::vector<RouteDefinition>& route_definitions
    ) const {
    using json = nlohmann::json;

    json doc;
    doc["openapi"] = "3.0.3";
    doc["info"] = {
        {"title", service.name + " API"},
        {"version", "1.0.0"},
        {"description", "Documentation OpenAPI generee automatiquement"}
    };

    doc["servers"] = json::array({
        {
            {"url", "http://localhost:" + std::to_string(service.port)}
        }
    });

    doc["paths"] = json::object();
    doc["components"] = {
        {"schemas", json::object()}
    };

    doc["components"]["securitySchemes"] = {
        {"bearerAuth", {
                           {"type", "http"},
                           {"scheme", "bearer"},
                           {"bearerFormat", "JWT"}
                       }}
    };

    for (const auto& entity : service.schema.entities) {
        doc["components"]["schemas"][entity.name] = make_entity_schema(entity);
        doc["components"]["schemas"][entity.name + "Input"] = make_entity_input_schema(entity);
    }

    for (const auto& route : route_definitions) {
        add_crud_path(doc["paths"], route, service);
    }

    add_relation_paths(doc["paths"], service);

    doc["paths"]["/health"]["get"] = {
        {"summary", "Health check"},
        {"responses", {
                          {"200", {
                                      {"description", "Service running"},
                                      {"content", {
                                                      {"application/json", {
                                                                               {"schema", {
                                                                                              {"type", "object"},
                                                                                              {"properties", {
                                                                                                                 {"status", {{"type", "string"}}}
                                                                                                             }}
                                                                                          }}
                                                                           }}
                                                  }}
                                  }}
                      }}
    };

    doc["paths"]["/auth/register"]["post"] = {
        {"summary", "Register"},
        {"requestBody", {
                            {"required", true},
                            {"content", {
                                            {"application/json", {
                                                                     {"schema", {
                                                                                    {"type", "object"},
                                                                                    {"properties", {
                                                                                                       {"email", {{"type", "string"}}},
                                                                                                       {"password", {{"type", "string"}}},
                                                                                                       {"full_name", {{"type", "string"}}}
                                                                                                   }},
                                                                                    {"required", json::array({"email", "password", "full_name"})}
                                                                                }}
                                                                 }}
                                        }}
                        }},
        {"responses", {
                          {"201", {{"description", "Created"}}},
                          {"400", {{"description", "Requete invalide"}}}
                      }}
    };

    doc["paths"]["/auth/login"]["post"] = {
        {"summary", "Login"},
        {"requestBody", {
                            {"required", true},
                            {"content", {
                                            {"application/json", {
                                                                     {"schema", {
                                                                                    {"type", "object"},
                                                                                    {"properties", {
                                                                                                       {"email", {{"type", "string"}}},
                                                                                                       {"password", {{"type", "string"}}}
                                                                                                   }},
                                                                                    {"required", json::array({"email", "password"})}
                                                                                }}
                                                                 }}
                                        }}
                        }},
        {"responses", {
                          {"200", {{"description", "Connexion reussie"}}},
                          {"401", {{"description", "Identifiants invalides"}}}
                      }}
    };

    doc["paths"]["/auth/me"]["get"] = {
        {"summary", "Current user"},
        {"security", json::array({
                         json{{"bearerAuth", json::array()}}
                     })},
        {"responses", {
                          {"200", {{"description", "Utilisateur courant"}}},
                          {"401", {{"description", "Token invalide ou absent"}}}
                      }}
    };

    return doc;
}

bool OpenApiGenerator::entity_requires_auth(
    const domain::Service& service,
    const std::string& entity_name
    ) const {
    for (const auto& entity : service.schema.entities) {
        if (entity.name == entity_name) {
            return entity.options.enable_auth;
        }
    }
    return false;
}

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

    const bool requires_auth = entity_requires_auth(service, route.entity_name);

    std::string entity_plural = "/" + route.entity_name;
    entity_plural[1] = static_cast<char>(
        std::tolower(static_cast<unsigned char>(entity_plural[1]))
        );
    entity_plural += "s";

    const std::string item_path = entity_plural + "/{id}";

    if (route.operation_name == "list") {
        json op = {
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
                                      }}
                          }}
        };

        if (requires_auth) {
            op["security"] = json::array({
                json{{"bearerAuth", json::array()}}
            });
        }

        paths[entity_plural][http_method] = op;
    }
    else if (route.operation_name == "create") {
        json op = {
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
                              {"400", {{"description", "Requete invalide"}}}
                          }}
        };

        if (requires_auth) {
            op["security"] = json::array({
                json{{"bearerAuth", json::array()}}
            });
        }

        paths[entity_plural][http_method] = op;
    }
    else if (route.operation_name == "get_by_id") {
        json op = {
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
                              {"404", {{"description", "Introuvable"}}}
                          }}
        };

        if (requires_auth) {
            op["security"] = json::array({
                json{{"bearerAuth", json::array()}}
            });
        }

        paths[item_path][http_method] = op;
    }
    else if (route.operation_name == "update") {
        json op = {
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
                              {"404", {{"description", "Introuvable"}}}
                          }}
        };

        if (requires_auth) {
            op["security"] = json::array({
                json{{"bearerAuth", json::array()}}
            });
        }

        paths[item_path][http_method] = op;
    }
    else if (route.operation_name == "delete") {
        json op = {
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
                              {"404", {{"description", "Introuvable"}}}
                          }}
        };

        if (requires_auth) {
            op["security"] = json::array({
                json{{"bearerAuth", json::array()}}
            });
        }

        paths[item_path][http_method] = op;
    }
}

void OpenApiGenerator::add_relation_paths(
    json& paths,
    const domain::Service& service
    ) const {
    for (const auto& entity : service.schema.entities) {
        const bool requires_auth = entity.options.enable_auth;

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
                                      {"400", {{"description", "Parametre invalide"}}}
                                  }}
                };

                if (requires_auth) {
                    by_id_op["security"] = json::array({
                        json{{"bearerAuth", json::array()}}
                    });
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
                                          {"404", {{"description", "Parent introuvable"}}}
                                      }}
                    };

                    if (requires_auth) {
                        by_field_op["security"] = json::array({
                            json{{"bearerAuth", json::array()}}
                        });
                    }

                    paths[by_field_path]["get"] = by_field_op;
                }

                // /parents_with_relation/{id}
                const std::string with_children_path =
                    parent_path + "_with_" + relation.name + "/{id}";

                json with_children_op = {
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
                                      {"404", {{"description", "Parent introuvable"}}}
                                  }}
                };

                if (requires_auth) {
                    with_children_op["security"] = json::array({
                        json{{"bearerAuth", json::array()}}
                    });
                }

                paths[with_children_path]["get"] = with_children_op;
            }

            if (relation.kind == sea::domain::RelationKind::HasOne) {
                const std::string relation_path =
                    parent_path + "/" + relation.name + "/{id}";

                json op = {
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
                                      {"404", {{"description", "Introuvable"}}}
                                  }}
                };

                if (requires_auth) {
                    op["security"] = json::array({
                        json{{"bearerAuth", json::array()}}
                    });
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
                                      {"400", {{"description", "Parametre invalide"}}}
                                  }}
                };

                if (requires_auth) {
                    op["security"] = json::array({
                        json{{"bearerAuth", json::array()}}
                    });
                }

                paths[by_id_path]["get"] = op;
            }
        }
    }
}

std::string OpenApiGenerator::to_openapi_method(HttpMethod method) const {
    switch (method) {
    case sea::application::HttpMethod::Get: return "get";
    case sea::application::HttpMethod::Post: return "post";
    case sea::application::HttpMethod::Put: return "put";
    case sea::application::HttpMethod::Delete: return "delete";
    default: return "";
    }
}

} // namespace sea::application