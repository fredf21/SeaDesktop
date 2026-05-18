#include "pagination_routes.h"
#include "paginated_match_rule.h"

#include "../handlers/pagination_handler/page_handler.h"
#include "../handlers/pagination_handler/offset_handlers.h"
#include "../handlers/pagination_handler/cursor_handlers.h"

#include "entity.h"
#include "relation.h"
#include "service.h"
#include "runtime/generic_crud_engine.h"
#include "spdlog/spdlog.h"

#include <optional>
#include <string>

namespace sea::http::routing {

namespace {

using sea::application::HttpMethod;
using sea::application::RouteDefinition;
namespace pgh = sea::http::handlers::pagination;

// ─────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────

bool service_has_auth(const sea::domain::Service& service)
{
    return service.security.authentication().type()
    != sea::domain::security::AuthType::None;
}

// Determine si une route est une route paginee (suffixe d'operation)
[[nodiscard]] std::optional<std::string>
pagination_mode_suffix(const std::string& op_name)
{
    static const char* const suffixes[] = { "_page", "_offset", "_cursor" };
    for (const char* s : suffixes) {
        const std::size_t slen = std::strlen(s);
        if (op_name.size() > slen &&
            op_name.compare(op_name.size() - slen, slen, s) == 0) {
            return std::string(s + 1);   // "page", "offset", "cursor"
        }
    }
    return std::nullopt;
}

// Extrait le nom d'operation base sans le suffixe pagination
// ex: "list_by_fk_page" -> "list_by_fk"
[[nodiscard]] std::string
strip_pagination_suffix(const std::string& op_name, const std::string& mode)
{
    const std::string full_suffix = "_" + mode;
    if (op_name.size() > full_suffix.size() &&
        op_name.compare(op_name.size() - full_suffix.size(),
                        full_suffix.size(), full_suffix) == 0) {
        return op_name.substr(0, op_name.size() - full_suffix.size());
    }
    return op_name;
}

// Recherche d'entite dans le schema par nom
[[nodiscard]] const sea::domain::Entity*
find_entity(const sea::domain::Schema& schema, const std::string& name)
{
    for (const auto& e : schema.entities) {
        if (e.name == name) return &e;
    }
    return nullptr;
}

// Pour une route /<children>/filter/with_<parent_lower>/{id}[/...],
// trouve la relation HasMany ou ManyToMany du parent qui pointe sur
// l'entite enfant. Retourne pointeur vers la relation + entite parente.
struct RelationLookup {
    const sea::domain::Entity*   parent;
    const sea::domain::Relation* relation;
};

[[nodiscard]] std::optional<RelationLookup>
find_parent_relation_for_child(
    const sea::domain::Schema& schema,
    const std::string& child_entity_name)
{
    for (const auto& parent : schema.entities) {
        for (const auto& relation : parent.relations) {
            if (relation.target_entity != child_entity_name) {
                continue;
            }
            // On considere HasMany et ManyToMany comme valides pour le filtrage
            if (relation.kind == sea::domain::RelationKind::HasMany ||
                relation.kind == sea::domain::RelationKind::ManyToMany) {
                return RelationLookup{ &parent, &relation };
            }
        }
    }
    return std::nullopt;
}

// Conversion HttpMethod -> seastar
[[nodiscard]] std::optional<seastar::httpd::operation_type>
to_seastar_operation(HttpMethod method)
{
    switch (method) {
    case HttpMethod::Get:    return seastar::httpd::operation_type::GET;
    case HttpMethod::Post:   return seastar::httpd::operation_type::POST;
    case HttpMethod::Put:    return seastar::httpd::operation_type::PUT;
    case HttpMethod::Delete: return seastar::httpd::operation_type::DELETE;
    }
    return std::nullopt;
}

// Log d'une route enregistree
void log_route(const std::string& method, const std::string& path,
               const std::string& handler_name, bool requires_auth)
{
    spdlog::get("sea.boot")->info(
        "[ROUTE-PAG] {} {} -> {} {}",
        method, path, handler_name, requires_auth ? " \xf0\x9f\x94\x92" : " \xf0\x9f\x8c\x90"
        );

}

// ─────────────────────────────────────────────────────────────────────
// Instanciation des handlers selon (op_base, mode)
//
// Retourne un unique_ptr<handler_base> ou nullptr si la config
// pagination du mode n'est pas activee pour cette entite.
// ─────────────────────────────────────────────────────────────────────

[[nodiscard]] std::unique_ptr<seastar::httpd::handler_base>
make_list_handler(
    const std::string& mode,
    const sea::domain::Entity& target_entity,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const MiddlewareContext& context)
{
    if (mode == "page" && target_entity.has_page_pagination()) {
        return std::make_unique<pgh::ListPageHandler>(
            crud_engine,
            target_entity.name,
            *target_entity.pagination->page,
            context.resource_auth_helper
            );
    }
    if (mode == "offset" && target_entity.has_offset_pagination()) {
        return std::make_unique<pgh::ListOffsetHandler>(
            crud_engine,
            target_entity.name,
            *target_entity.pagination->offset,
            context.resource_auth_helper
            );
    }
    if (mode == "cursor" && target_entity.has_cursor_pagination()) {
        return std::make_unique<pgh::ListCursorHandler>(
            crud_engine,
            target_entity.name,
            *target_entity.pagination->cursor,
            context.resource_auth_helper
            );
    }
    return nullptr;
}

[[nodiscard]] std::unique_ptr<seastar::httpd::handler_base>
make_list_by_fk_handler(
    const std::string& mode,
    const sea::domain::Entity& target_entity,
    const std::string& fk_column,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const MiddlewareContext& context)
{
    if (mode == "page" && target_entity.has_page_pagination()) {
        return std::make_unique<pgh::ListByFkPageHandler>(
            crud_engine,
            target_entity.name,
            fk_column,
            *target_entity.pagination->page,
            context.resource_auth_helper
            );
    }
    if (mode == "offset" && target_entity.has_offset_pagination()) {
        return std::make_unique<pgh::ListByFkOffsetHandler>(
            crud_engine,
            target_entity.name,
            fk_column,
            *target_entity.pagination->offset,
            context.resource_auth_helper
            );
    }
    if (mode == "cursor" && target_entity.has_cursor_pagination()) {
        return std::make_unique<pgh::ListByFkCursorHandler>(
            crud_engine,
            target_entity.name,
            fk_column,
            *target_entity.pagination->cursor,
            context.resource_auth_helper
            );
    }
    return nullptr;
}

[[nodiscard]] std::unique_ptr<seastar::httpd::handler_base>
make_list_by_fk_field_handler(
    const std::string& mode,
    const sea::domain::Entity& target_entity,
    const std::string& parent_entity_name,
    const std::string& fk_column,
    const std::string& search_field,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const MiddlewareContext& context)
{
    if (mode == "page" && target_entity.has_page_pagination()) {
        return std::make_unique<pgh::ListByFkFieldPageHandler>(
            crud_engine,
            target_entity.name,
            parent_entity_name,
            fk_column,
            search_field,
            *target_entity.pagination->page,
            context.resource_auth_helper
            );
    }
    if (mode == "offset" && target_entity.has_offset_pagination()) {
        return std::make_unique<pgh::ListByFkFieldOffsetHandler>(
            crud_engine,
            target_entity.name,
            parent_entity_name,
            fk_column,
            search_field,
            *target_entity.pagination->offset,
            context.resource_auth_helper
            );
    }
    if (mode == "cursor" && target_entity.has_cursor_pagination()) {
        return std::make_unique<pgh::ListByFkFieldCursorHandler>(
            crud_engine,
            target_entity.name,
            parent_entity_name,
            fk_column,
            search_field,
            *target_entity.pagination->cursor,
            context.resource_auth_helper
            );
    }
    return nullptr;
}

[[nodiscard]] std::unique_ptr<seastar::httpd::handler_base>
make_list_m2m_handler(
    const std::string& mode,
    const sea::domain::Entity& target_entity,
    const std::string& pivot_table,
    const std::string& source_fk_column,
    const std::string& target_fk_column,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const MiddlewareContext& context)
{
    if (mode == "page" && target_entity.has_page_pagination()) {
        return std::make_unique<pgh::ListManyToManyPageHandler>(
            crud_engine,
            target_entity.name,
            pivot_table,
            source_fk_column,
            target_fk_column,
            *target_entity.pagination->page,
            context.resource_auth_helper
            );
    }
    if (mode == "offset" && target_entity.has_offset_pagination()) {
        return std::make_unique<pgh::ListManyToManyOffsetHandler>(
            crud_engine,
            target_entity.name,
            pivot_table,
            source_fk_column,
            target_fk_column,
            *target_entity.pagination->offset,
            context.resource_auth_helper
            );
    }
    if (mode == "cursor" && target_entity.has_cursor_pagination()) {
        return std::make_unique<pgh::ListManyToManyCursorHandler>(
            crud_engine,
            target_entity.name,
            pivot_table,
            source_fk_column,
            target_fk_column,
            *target_entity.pagination->cursor,
            context.resource_auth_helper
            );
    }
    return nullptr;
}

[[nodiscard]] std::unique_ptr<seastar::httpd::handler_base>
make_get_with_children_handler(
    const std::string& mode,
    const sea::domain::Entity& parent_entity,
    const sea::domain::Entity& child_entity,
    const std::string& fk_column,
    const std::string& children_key,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const MiddlewareContext& context)
{
    // C'est l'ENFANT qui porte la pagination (puisque ce sont
    // les enfants imbriques qui sont pagines)
    if (mode == "page" && child_entity.has_page_pagination()) {
        return std::make_unique<pgh::GetWithChildrenPageHandler>(
            crud_engine,
            parent_entity.name,
            child_entity.name,
            fk_column,
            children_key,
            *child_entity.pagination->page,
            context.resource_auth_helper
            );
    }
    if (mode == "offset" && child_entity.has_offset_pagination()) {
        return std::make_unique<pgh::GetWithChildrenOffsetHandler>(
            crud_engine,
            parent_entity.name,
            child_entity.name,
            fk_column,
            children_key,
            *child_entity.pagination->offset,
            context.resource_auth_helper
            );
    }
    if (mode == "cursor" && child_entity.has_cursor_pagination()) {
        return std::make_unique<pgh::GetWithChildrenCursorHandler>(
            crud_engine,
            parent_entity.name,
            child_entity.name,
            fk_column,
            children_key,
            *child_entity.pagination->cursor,
            context.resource_auth_helper
            );
    }
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────
// Dispatch principal pour une route donnee
// ─────────────────────────────────────────────────────────────────────

void register_one_paginated_route(
    seastar::httpd::routes& routes,
    const RouteDefinition& route,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const MiddlewareContext& context)
{
    const auto mode_opt = pagination_mode_suffix(route.operation_name);
    if (!mode_opt.has_value()) {
        return;   // Pas une route paginee
    }
    const std::string mode = *mode_opt;
    const std::string op_base = strip_pagination_suffix(route.operation_name, mode);

    const auto operation = to_seastar_operation(route.method);
    if (!operation.has_value()) {
        return;
    }

    const bool requires_auth = service_has_auth(context.service);
    const auto& schema = context.service.schema;

    // L'entite cible (target) est celle paginee dans la reponse :
    // - list_*           -> entity_name
    // - list_by_fk_*     -> entity_name (enfants)
    // - list_by_fk_field_* -> entity_name (enfants)
    // - list_many_to_many_* -> entity_name (cibles M2M)
    // - get_with_children_* -> entity_name (parent), mais c'est l'enfant
    //                          qui porte la pagination (handled specifically)
    const auto* target_entity = find_entity(schema, route.entity_name);
    if (target_entity == nullptr) {
        return;
    }

    std::unique_ptr<seastar::httpd::handler_base> handler;
    std::string handler_name;

    // ─── list ────────────────────────────────────────────────────────
    if (op_base == "list") {
        handler = make_list_handler(mode, *target_entity, crud_engine, context);
        handler_name = "List" + mode + "Handler";
    }
    // ─── list_by_fk ──────────────────────────────────────────────────
    else if (op_base == "list_by_fk") {
        // Le path est /<children>/filter/with_<parent_lower>/{id}/<mode>
        // On retrouve la relation HasMany sur le parent pour avoir fk_column
        const auto rel = find_parent_relation_for_child(schema, target_entity->name);
        if (!rel.has_value() || rel->relation->kind != sea::domain::RelationKind::HasMany) {
            return;
        }
        handler = make_list_by_fk_handler(
            mode, *target_entity, rel->relation->fk_column, crud_engine, context
            );
        handler_name = "ListByFk" + mode + "Handler";
    }
    // ─── list_by_fk_field ────────────────────────────────────────────
    else if (op_base == "list_by_fk_field") {
        const auto rel = find_parent_relation_for_child(schema, target_entity->name);
        if (!rel.has_value() || rel->relation->kind != sea::domain::RelationKind::HasMany) {
            return;
        }
        // Le search_field est extrait du path : .../with_<parent>_<field>/{value}/<mode>
        // On parse depuis le path : on extrait <field>.
        // Convention de route_generator : /filter/with_<parentlower>_<fieldname>/{value}/<mode>
        const std::string& path = route.path;
        const auto with_pos = path.find("/filter/with_");
        if (with_pos == std::string::npos) return;

        const std::size_t prefix_end = with_pos + std::strlen("/filter/with_");
        const std::size_t slash_pos = path.find('/', prefix_end);
        if (slash_pos == std::string::npos) return;

        const std::string parent_and_field = path.substr(prefix_end, slash_pos - prefix_end);
        const auto underscore_pos = parent_and_field.find('_');
        if (underscore_pos == std::string::npos) return;

        const std::string search_field = parent_and_field.substr(underscore_pos + 1);

        handler = make_list_by_fk_field_handler(
            mode, *target_entity, rel->parent->name,
            rel->relation->fk_column, search_field, crud_engine, context
            );
        handler_name = "ListByFkField" + mode + "Handler";
    }
    // ─── list_many_to_many ───────────────────────────────────────────
    else if (op_base == "list_many_to_many") {
        const auto rel = find_parent_relation_for_child(schema, target_entity->name);
        if (!rel.has_value() || rel->relation->kind != sea::domain::RelationKind::ManyToMany) {
            return;
        }
        handler = make_list_m2m_handler(
            mode, *target_entity,
            rel->relation->pivot_table,
            rel->relation->source_fk_column,
            rel->relation->target_fk_column,
            crud_engine, context
            );
        handler_name = "ListManyToMany" + mode + "Handler";
    }
    // ─── get_with_children ───────────────────────────────────────────
    else if (op_base == "get_with_children") {
        // Ici entity_name = parent. On cherche la relation HasMany du parent.
        const auto& parent = *target_entity;
        const sea::domain::Relation* gwc_relation = nullptr;
        for (const auto& r : parent.relations) {
            if (r.kind != sea::domain::RelationKind::HasMany) continue;
            // Le path contient _with_<relation.name>/
            if (route.path.find("_with_" + r.name + "/") != std::string::npos) {
                gwc_relation = &r;
                break;
            }
        }
        if (gwc_relation == nullptr) return;

        const auto* child = find_entity(schema, gwc_relation->target_entity);
        if (child == nullptr) return;

        handler = make_get_with_children_handler(
            mode, parent, *child,
            gwc_relation->fk_column,
            gwc_relation->name,           // children_key dans la reponse JSON
            crud_engine, context
            );
        handler_name = "GetWithChildren" + mode + "Handler";
    }
    else {
        return;   // op_base inconnu, on skip
    }

    if (!handler) {
        // Mode pas active dans la config -> on skip silencieusement
        // (defensive : ne devrait pas arriver puisque route_generator
        //  n'aurait pas genere la route)
        return;
    }

    log_route(
        route.method == HttpMethod::Get ? "GET" : "?",
        route.path,
        handler_name,
        requires_auth
        );

    // Wrap avec les middlewares (auth, rate limit, etc.) -- meme logique
    // que les routes non paginees
    auto wrapped = wrap_with_middlewares(
        std::move(handler),
        requires_auth,
        context
        );

    // Construction du match_rule a partir du path-template
    auto* rule = build_match_rule_from_template(route.path, wrapped.release());
    routes.add(rule, *operation);
}

} // namespace anonyme


// ─────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────

void register_pagination_routes(
    seastar::httpd::routes& routes,
    const std::vector<RouteDefinition>& route_definitions,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const MiddlewareContext& context)
{
    for (const auto& route : route_definitions) {
        if (!pagination_mode_suffix(route.operation_name).has_value()) {
            continue;
        }
        register_one_paginated_route(routes, route, crud_engine, context);
    }
}

} // namespace sea::http::routing
