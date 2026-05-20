#include "route_generator.h"
#include "service.h"

#include <cctype>

namespace sea::application {

namespace {

std::string lower_first(std::string value) {
    if (!value.empty()) {
        value[0] = static_cast<char>(
            std::tolower(static_cast<unsigned char>(value[0]))
            );
    }
    return value;
}

std::string plural_path_from_entity(const std::string& entity_name) {
    return "/" + lower_first(entity_name) + "s";
}

bool schema_has_auth_source(const sea::domain::Schema& schema) {
    return std::ranges::any_of(schema.entities,
                               [](const sea::domain::Entity& e) {
                                   return e.options.is_auth_source;
                               }
                               );
}
// Ajoute /page, /offset, /cursor a un chemin de base, en fonction des
// modes actives dans la PaginationConfig de l'entite. Si la config
// est absente, n'ajoute rien.
//
// Parametres :
// - routes        : vecteur dans lequel pousser les nouvelles routes
// - entity        : entite source (pour lire entity.pagination)
// - base_path     : chemin de base auquel ajouter /page, /offset, /cursor
//                   (ex: "/users", "/users/filter/with_department/{id}")
// - target_entity : nom d'entite a mettre dans RouteDefinition.entity_name
// - op_prefix     : prefixe d'operation_name (ex: "list", "list_by_fk")
//                   suffixe ajoute : "_page", "_offset", "_cursor"
// - requires_auth : meme valeur que la route non paginee equivalente
void append_pagination_variants(
    std::vector<RouteDefinition>& routes,
    const sea::domain::Entity& entity,
    const std::string& base_path,
    const std::string& target_entity,
    const std::string& op_prefix,
    bool requires_auth)
{
    if (!entity.has_pagination()) {
        return;
    }

    if (entity.has_page_pagination()) {
        routes.push_back({
            .method = HttpMethod::Get,
            .path = base_path + "/page",
            .entity_name = target_entity,
            .operation_name = op_prefix + "_page",
            .requires_auth = requires_auth
        });
    }

    if (entity.has_offset_pagination()) {
        routes.push_back({
            .method = HttpMethod::Get,
            .path = base_path + "/offset",
            .entity_name = target_entity,
            .operation_name = op_prefix + "_offset",
            .requires_auth = requires_auth
        });
    }

    if (entity.has_cursor_pagination()) {
        routes.push_back({
            .method = HttpMethod::Get,
            .path = base_path + "/cursor",
            .entity_name = target_entity,
            .operation_name = op_prefix + "_cursor",
            .requires_auth = requires_auth
        });
    }
}

// Cherche une entite par nom dans un schema (helper pour append_paginated_relation_routes)
const sea::domain::Entity* find_entity_in_schema(
    const sea::domain::Schema& schema,
    const std::string& name)
{
    for (const auto& e : schema.entities) {
        if (e.name == name) return &e;
    }
    return nullptr;
}

} // namespace

std::vector<RouteDefinition>
RouteGenerator::generate(const sea::domain::Service& service) const {
    std::vector<RouteDefinition> routes;

    // Détermine une fois pour toutes si l'auth est activée au niveau service
    const bool needs_auth = service_requires_auth(service);

    const auto& schema = service.schema;

    // CRUD
    for (const auto& entity : schema.entities) {
        auto entity_routes = generate_for_entity(entity, needs_auth);
        routes.insert(routes.end(), entity_routes.begin(), entity_routes.end());
    }

    // Auth globale réelle
    if (schema_has_auth_source(schema)) {
        // Routes publiques
        routes.push_back({
            .method = HttpMethod::Post,
            .path = "/auth/register",
            .entity_name = "Auth",
            .operation_name = "register",
            .requires_auth = false                  // public
        });

        routes.push_back({
            .method = HttpMethod::Post,
            .path = "/auth/login",
            .entity_name = "Auth",
            .operation_name = "login",
            .requires_auth = false                  // public
        });

        // Route protégée
        routes.push_back({
            .method = HttpMethod::Get,
            .path = "/auth/me",
            .entity_name = "Auth",
            .operation_name = "me",
            .requires_auth = true                   // protégée (besoin du JWT)
        });

        // ajouts utiles si auth activée
        routes.push_back({
            .method = HttpMethod::Post,
            .path = "/auth/refresh",
            .entity_name = "Auth",
            .operation_name = "refresh",
            .requires_auth = false                  // public (utilise refresh_token)
        });

        routes.push_back({
            .method = HttpMethod::Post,
            .path = "/auth/logout",
            .entity_name = "Auth",
            .operation_name = "logout",
            .requires_auth = true                   // protégée
        });
    }

    // Relations
    for (const auto& entity : schema.entities) {
        const std::string parent_path = plural_path_from_entity(entity.name);
        const std::string parent_name = lower_first(entity.name);

        for (const auto& relation : entity.relations) {
            if (relation.kind == sea::domain::RelationKind::HasMany) {
                const std::string child_path = plural_path_from_entity(relation.target_entity);

                // /children/filter/with_parent/{id}
                routes.push_back({
                    .method = HttpMethod::Get,
                    .path = child_path + "/filter/with_" + parent_name + "/{id}",
                    .entity_name = relation.target_entity,
                    .operation_name = "list_by_fk",
                    .requires_auth = needs_auth         // ← AJOUT
                });
                const auto* target_entity_ptr = find_entity_in_schema(schema, relation.target_entity);
                if (target_entity_ptr != nullptr) {
                    append_pagination_variants(
                        routes,
                        *target_entity_ptr,
                        child_path + "/filter/with_" + parent_name + "/{id}",
                        relation.target_entity,
                        "list_by_fk",
                        needs_auth
                        );
                }


                // /children/filter/with_parent_<unique_field>/{value}
                for (const auto& field : entity.fields) {
                    if (!field.unique || field.name == "id") {
                        continue;
                    }

                    routes.push_back({
                        .method = HttpMethod::Get,
                        .path = child_path + "/filter/with_" + parent_name + "_" + field.name + "/{value}",
                        .entity_name = relation.target_entity,
                        .operation_name = "list_by_fk_field",
                        .requires_auth = needs_auth     // ← AJOUT
                    });
                    if (target_entity_ptr != nullptr) {
                        append_pagination_variants(
                            routes,
                            *target_entity_ptr,
                            child_path + "/filter/with_" + parent_name + "_" + field.name + "/{value}",
                            relation.target_entity,
                            "list_by_fk_field",
                            needs_auth
                            );
                    }

                }

                // /parents_with_relation/{id}
                routes.push_back({
                    .method = HttpMethod::Get,
                    .path = parent_path + "_with_" + relation.name + "/{id}",
                    .entity_name = entity.name,
                    .operation_name = "get_with_children",
                    .requires_auth = needs_auth         // ← AJOUT
                });
                if (target_entity_ptr != nullptr) {
                    append_pagination_variants(
                        routes,
                        *target_entity_ptr,           // on lit la pagination de l'enfant
                        parent_path + "_with_" + relation.name + "/{id}",
                        entity.name,                  // mais entity_name reste le parent
                        "get_with_children",
                        needs_auth
                        );
                }

            }

            if (relation.kind == sea::domain::RelationKind::HasOne) {
                routes.push_back({
                    .method = HttpMethod::Get,
                    .path = parent_path + "/" + relation.name + "/{id}",
                    .entity_name = relation.target_entity,
                    .operation_name = "get_one_by_fk",
                    .requires_auth = needs_auth         // ← AJOUT
                });
                // ─────────────────────────────────────────────────────────────────────
                // PATCH 3 — HasOne : EXCLUSION VOLONTAIRE
                //
                // Le bloc HasOne ne re\u00e7oit AUCUNE variante paginée. C'est une
                // décision de design, pas un oubli.
                //
                // Justification :
                // - HasOne retourne par définition un singleton (0 ou 1 enregistrement).
                // - Paginer un singleton n'a aucun sens fonctionnel : la réponse
                //   contiendrait toujours au plus 1 élément.
                // - Cohérent avec get_by_id, qui n'est pas paginé non plus pour
                //   la meme raison.
                // - Conforme aux conventions REST (JSON:API, HAL, etc. ne paginent
                //   jamais les singletons).
                //
                // Ajouter le commentaire suivant DANS le bloc HasOne du route_generator,
                // JUSTE APRES le push_back("get_one_by_fk", ...) :
                //
                //     // Note : HasOne ne re\u00e7oit pas de variante paginée.
                //     // Une relation HasOne retourne un singleton, donc la pagination
                //     // n'a pas de sens ici. Meme logique que get_by_id.
                //
                // ─────────────────────────────────────────────────────────────────────

            }
            if (relation.kind == sea::domain::RelationKind::ManyToMany) {
                const std::string target_path = plural_path_from_entity(relation.target_entity);

                // ─── GET /<target>s/filter/with_<parent>/{id} (existant) ───
                routes.push_back({
                    .method = HttpMethod::Get,
                    .path = target_path + "/filter/with_" + parent_name + "/{id}",
                    .entity_name = relation.target_entity,
                    .operation_name = "list_many_to_many",
                    .requires_auth = needs_auth
                });

                const auto* m2m_target = find_entity_in_schema(schema, relation.target_entity);
                if (m2m_target != nullptr) {
                    append_pagination_variants(
                        routes,
                        *m2m_target,
                        target_path + "/filter/with_" + parent_name + "/{id}",
                        relation.target_entity,
                        "list_many_to_many",
                        needs_auth
                        );
                }

                // ─── POST /<entity>s/{id}/<relation>/{target_id} (NOUVEAU) ───
                //
                // Cree une association dans la table pivot.
                //
                // entity_name est volontairement celui de l'ENTITE SOURCE
                // (pas la cible), car c'est elle qui porte la relation et
                // c'est sur elle qu'OpenAPI groupe la route via le tag.
                routes.push_back({
                    .method = HttpMethod::Post,
                    .path = parent_path + "/{id}/" + relation.name + "/{target_id}",
                    .entity_name = entity.name,
                    .operation_name = "attach_many_to_many",
                    .requires_auth = needs_auth
                });

                // ─── DELETE /<entity>s/{id}/<relation>/{target_id} (NOUVEAU) ───
                routes.push_back({
                    .method = HttpMethod::Delete,
                    .path = parent_path + "/{id}/" + relation.name + "/{target_id}",
                    .entity_name = entity.name,
                    .operation_name = "detach_many_to_many",
                    .requires_auth = needs_auth
                });
            }


        }
    }

    return routes;
}

std::vector<RouteDefinition>
RouteGenerator::generate_for_entity(const sea::domain::Entity& entity, bool requires_auth) const {
    std::vector<RouteDefinition> routes;

    if (!entity.options.enable_crud) {
        return routes;
    }

    const std::string base_path = entity.route_prefix();

    routes.push_back({
        .method = HttpMethod::Get,
        .path = base_path,
        .entity_name = entity.name,
        .operation_name = "list",
        .requires_auth = requires_auth
    });

    routes.push_back({
        .method = HttpMethod::Get,
        .path = base_path + "/{id}",
        .entity_name = entity.name,
        .operation_name = "get_by_id",
        .requires_auth = requires_auth
    });

    routes.push_back({
        .method = HttpMethod::Post,
        .path = base_path,
        .entity_name = entity.name,
        .operation_name = "create",
        .requires_auth = requires_auth
    });

    routes.push_back({
        .method = HttpMethod::Put,
        .path = base_path + "/{id}",
        .entity_name = entity.name,
        .operation_name = "update",
        .requires_auth = requires_auth
    });

    routes.push_back({
        .method = HttpMethod::Delete,
        .path = base_path + "/{id}",
        .entity_name = entity.name,
        .operation_name = "delete",
        .requires_auth = requires_auth
    });
    // ── Pagination des routes CRUD list ────────────────────────
    // Ajoute /users/page, /users/offset, /users/cursor selon les modes
    // declares dans entity.pagination
    append_pagination_variants(
        routes,
        entity,
        base_path,              // ex: "/users"
        entity.name,            // target = soi-meme
        "list",                 // op_prefix
        requires_auth
        );

    return routes;
    // ─────────────────────────────────────────────────────────────────────
    // PATCH 4 — get_by_id : EXCLUSION VOLONTAIRE également
    //
    // Pour les memes raisons, get_by_id (GET /users/{id}) ne re\u00e7oit pas
    // de variante paginée. Aucun changement nécessaire dans le
    // generate_for_entity, mais le RouteAuthorizationResolver a une garde
    // explicite pour rejeter les fausses routes du type /users/{id}/page
    // (voir patch resolver, PATCH 3).
    // ─────────────────────────────────────────────────────────────────────

    // ═══════════════════════════════════════════════════════════════════
    // Resume des operation_name introduits par cette etape (15 max/entite) :
    //
    //   list_page                       GET /users/page
    //   list_offset                     GET /users/offset
    //   list_cursor                     GET /users/cursor
    //
    //   list_by_fk_page                 GET /users/filter/with_X/{id}/page
    //   list_by_fk_offset               GET /users/filter/with_X/{id}/offset
    //   list_by_fk_cursor               GET /users/filter/with_X/{id}/cursor
    //
    //   list_by_fk_field_page           GET /users/filter/with_X_Y/{value}/page
    //   list_by_fk_field_offset         GET /users/filter/with_X_Y/{value}/offset
    //   list_by_fk_field_cursor         GET /users/filter/with_X_Y/{value}/cursor
    //
    //   list_many_to_many_page          GET /users/filter/with_X/{id}/page
    //   list_many_to_many_offset        GET /users/filter/with_X/{id}/offset
    //   list_many_to_many_cursor        GET /users/filter/with_X/{id}/cursor
    //
    //   get_with_children_page          GET /Xs_with_Y/{id}/page
    //   get_with_children_offset        GET /Xs_with_Y/{id}/offset
    //   get_with_children_cursor        GET /Xs_with_Y/{id}/cursor
    //
    // Exclusions volontaires (singletons \u2014 pas de sens \u00e0 paginer) :
    //   get_by_id        GET /users/{id}                  (1 record)
    //   get_one_by_fk    GET /users/profile/{id}  HasOne  (1 record)
    // ═══════════════════════════════════════════════════════════════════

}
bool RouteGenerator::service_requires_auth(
    const sea::domain::Service& service) const
{
    // Si l'auth est désactivée au niveau service → toutes les routes CRUD publiques
    // Si l'auth est activée → toutes les routes CRUD protégées

    return service.security.authentication().type()
           != sea::domain::security::AuthType::None;
}
} // namespace sea::application