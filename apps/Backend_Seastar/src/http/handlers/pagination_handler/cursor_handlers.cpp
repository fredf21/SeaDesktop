#include "cursor_handlers.h"

#include "../access_control/resource_authorization_helper.h"
#include "../../utils/http_utils.h"
#include "../../utils/pagination_query.h"

#include "access_control/crud_operation.h"
#include "runtime/generic_crud_engine.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <string_view>
#include <utility>

namespace sea::http::handlers::pagination {

namespace {

using json = nlohmann::json;
using sea::infrastructure::runtime::DynamicRecord;
using sea::infrastructure::persistence::CursorRequest;
using sea::infrastructure::persistence::CursorResult;

// ─────────────────────────────────────────────────────────────────────
// Helper interne : construit l'enveloppe JSON cursor.
//
// Pas de 'total' (volontaire : le mode cursor evite COUNT(*)).
// 'next_cursor' present uniquement s'il y a une page suivante.
// ─────────────────────────────────────────────────────────────────────
[[nodiscard]] std::string build_cursor_envelope(
    const std::string& items_json,
    const CursorRequest& request,
    const std::optional<std::string>& next_cursor)
{
    std::ostringstream oss;
    oss << "{\"items\":" << items_json
        << ",\"limit\":" << request.limit;

    if (next_cursor.has_value()) {
        oss << ",\"next_cursor\":\""
            << sea::http::utils::json_escape(*next_cursor)
            << "\"";
    }

    oss << "}";
    return oss.str();
}

[[nodiscard]] std::unique_ptr<seastar::http::reply> bad_request(
    std::unique_ptr<seastar::http::reply> rep,
    const std::string& message)
{
    rep->set_status(seastar::http::reply::status_type::bad_request);
    rep->write_body("application/json",
                    json{{"error", "Bad Request"}, {"message", message}}.dump());
    return rep;
}

[[nodiscard]] std::string apply_abac_filter(
    const std::string& records_json,
    const std::string& entity_name,
    const std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper>& auth_helper,
    const seastar::http::request& req)
{
    if (!auth_helper) {
        return records_json;
    }

    const auto subject = auth_helper->build_subject_from_headers(req);
    const std::string path_str{std::string_view(req._url)};
    const auto context = auth_helper->build_context(req, path_str);

    return auth_helper->filter_collection(
        entity_name,
        sea::domain::access_control::CrudOperation::List,
        subject,
        records_json,
        context
        );
}

// Helper pour les listings filtres en mode cursor : extrait la valeur
// du cursor_field d'un record pour construire next_cursor manuellement.
//
// Necessaire car en cas de filtrage cote handler (list_by_fk en cursor),
// le next_cursor renvoye par le repo correspond au DERNIER record AVANT
// filtrage. Apres filtrage, on doit reconstruire le cursor a partir du
// dernier record retenu.
[[nodiscard]] std::optional<std::string>
extract_cursor_value(const DynamicRecord& record, const std::string& cursor_field)
{
    const auto it = record.find(cursor_field);
    if (it == record.end()) return std::nullopt;
    return sea::http::utils::dynamic_value_to_string_id(it->second);
}

} // namespace anonyme


// ═════════════════════════════════════════════════════════════════════
// ListCursorHandler
// ═════════════════════════════════════════════════════════════════════

ListCursorHandler::ListCursorHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::string entity_name,
    sea::domain::CursorPagination config,
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper)
    : crud_engine_(std::move(crud_engine))
    , entity_name_(std::move(entity_name))
    , config_(std::move(config))
    , auth_helper_(std::move(auth_helper))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
ListCursorHandler::handle(const seastar::sstring&,
                          std::unique_ptr<seastar::http::request> req,
                          std::unique_ptr<seastar::http::reply> rep)
{
    auto parsed = sea::http::utils::parse_cursor_query(*req, config_);
    if (!parsed.ok()) {
        co_return bad_request(std::move(rep), parsed.error.value_or("Invalid query"));
    }

    const CursorRequest& request = *parsed.request;
    const CursorResult page = co_await crud_engine_->list_cursor(entity_name_, request);

    const std::string items_json = sea::http::utils::records_to_json(page.items);
    const std::string filtered = apply_abac_filter(items_json, entity_name_, auth_helper_, *req);
    const std::string envelope = build_cursor_envelope(filtered, request, page.next_cursor);

    rep->set_status(seastar::http::reply::status_type::ok);
    rep->write_body("application/json", envelope);
    co_return std::move(rep);
}


// ═════════════════════════════════════════════════════════════════════
// ListByFkCursorHandler
//
// Particularite mode cursor : si on filtre apres la requete, le
// next_cursor du repo n'est plus valide (il pointe sur un record qu'on
// a peut-etre exclu). On reconstruit next_cursor a partir du dernier
// record retenu si la page repo etait pleine.
//
// Limitation MVP : si le filtrage exclut TOUS les records de la page,
// on perd la continuite (next_cursor = nullopt alors qu'il y a peut-etre
// d'autres records valides plus loin). A ameliorer avec un filtre SQL.
// ═════════════════════════════════════════════════════════════════════

ListByFkCursorHandler::ListByFkCursorHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::string child_entity,
    std::string fk_column,
    sea::domain::CursorPagination config,
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper)
    : crud_engine_(std::move(crud_engine))
    , child_entity_(std::move(child_entity))
    , fk_column_(std::move(fk_column))
    , config_(std::move(config))
    , auth_helper_(std::move(auth_helper))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
ListByFkCursorHandler::handle(const seastar::sstring&,
                              std::unique_ptr<seastar::http::request> req,
                              std::unique_ptr<seastar::http::reply> rep)
{
    const auto parent_id_sstring = std::string(sea::http::utils::strip_leading_slash(req->get_path_param("id")));
    if (parent_id_sstring.empty()) {
        co_return bad_request(std::move(rep), "id manquant");
    }
    const std::string parent_id{std::string_view(parent_id_sstring)};

    auto parsed = sea::http::utils::parse_cursor_query(*req, config_);
    if (!parsed.ok()) {
        co_return bad_request(std::move(rep), parsed.error.value_or("Invalid query"));
    }

    const CursorRequest& request = *parsed.request;
    const CursorResult page = co_await crud_engine_->list_cursor(child_entity_, request);

    std::vector<DynamicRecord> filtered;
    filtered.reserve(page.items.size());
    for (const auto& r : page.items) {
        const auto it = r.find(fk_column_);
        if (it == r.end()) continue;
        const auto id = sea::http::utils::dynamic_value_to_string_id(it->second);
        if (id.has_value() && *id == parent_id) {
            filtered.push_back(r);
        }
    }

    // Recalcul du next_cursor : si le repo en avait un et qu'on a au moins
    // un record retenu, on prend le cursor_field du dernier retenu.
    std::optional<std::string> next_cursor;
    if (page.next_cursor.has_value() && !filtered.empty()) {
        next_cursor = extract_cursor_value(filtered.back(), config_.cursor_field);
    }

    const std::string items_json = sea::http::utils::records_to_json(filtered);
    const std::string abac_filtered = apply_abac_filter(items_json, child_entity_, auth_helper_, *req);
    const std::string envelope = build_cursor_envelope(abac_filtered, request, next_cursor);

    rep->set_status(seastar::http::reply::status_type::ok);
    rep->write_body("application/json", envelope);
    co_return std::move(rep);
}


// ═════════════════════════════════════════════════════════════════════
// ListByFkFieldCursorHandler
// ═════════════════════════════════════════════════════════════════════

ListByFkFieldCursorHandler::ListByFkFieldCursorHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::string child_entity,
    std::string parent_entity,
    std::string fk_column,
    std::string search_field,
    sea::domain::CursorPagination config,
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper)
    : crud_engine_(std::move(crud_engine))
    , child_entity_(std::move(child_entity))
    , parent_entity_(std::move(parent_entity))
    , fk_column_(std::move(fk_column))
    , search_field_(std::move(search_field))
    , config_(std::move(config))
    , auth_helper_(std::move(auth_helper))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
ListByFkFieldCursorHandler::handle(const seastar::sstring&,
                                   std::unique_ptr<seastar::http::request> req,
                                   std::unique_ptr<seastar::http::reply> rep)
{
    const auto value_sstring = std::string(sea::http::utils::strip_leading_slash(req->get_path_param("value")));
    if (value_sstring.empty()) {
        co_return bad_request(std::move(rep), "value manquant");
    }
    const std::string value{std::string_view(value_sstring)};

    auto parsed = sea::http::utils::parse_cursor_query(*req, config_);
    if (!parsed.ok()) {
        co_return bad_request(std::move(rep), parsed.error.value_or("Invalid query"));
    }

    // 1. Resolution du parent par search_field
    const auto parents = co_await crud_engine_->list(parent_entity_);
    std::optional<std::string> parent_id;
    for (const auto& p : parents) {
        const auto field_it = p.find(search_field_);
        if (field_it == p.end()) continue;
        if (!sea::http::utils::dynamic_value_matches_string(field_it->second, value)) {
            continue;
        }
        const auto id_it = p.find("id");
        if (id_it == p.end()) continue;
        parent_id = sea::http::utils::dynamic_value_to_string_id(id_it->second);
        break;
    }

    if (!parent_id.has_value()) {
        rep->set_status(seastar::http::reply::status_type::not_found);
        rep->write_body("application/json",
                        json{{"error", "parent introuvable"}}.dump());
        co_return std::move(rep);
    }

    const CursorRequest& request = *parsed.request;
    const CursorResult page = co_await crud_engine_->list_cursor(child_entity_, request);

    std::vector<DynamicRecord> filtered;
    filtered.reserve(page.items.size());
    for (const auto& r : page.items) {
        const auto it = r.find(fk_column_);
        if (it == r.end()) continue;
        const auto id = sea::http::utils::dynamic_value_to_string_id(it->second);
        if (id.has_value() && *id == *parent_id) {
            filtered.push_back(r);
        }
    }

    std::optional<std::string> next_cursor;
    if (page.next_cursor.has_value() && !filtered.empty()) {
        next_cursor = extract_cursor_value(filtered.back(), config_.cursor_field);
    }

    const std::string items_json = sea::http::utils::records_to_json(filtered);
    const std::string abac_filtered = apply_abac_filter(items_json, child_entity_, auth_helper_, *req);
    const std::string envelope = build_cursor_envelope(abac_filtered, request, next_cursor);

    rep->set_status(seastar::http::reply::status_type::ok);
    rep->write_body("application/json", envelope);
    co_return std::move(rep);
}


// ═════════════════════════════════════════════════════════════════════
// ListManyToManyCursorHandler
//
// Particularite : le cursor ici n'a pas le meme sens qu'un cursor SQL.
// On resout la liste M2M complete, on trie par cursor_field, puis on
// applique la logique "after" en memoire.
//
// Limitation : full scan du pivot a chaque requete. Acceptable pour
// des cardinalites raisonnables (< 10k).
// ═════════════════════════════════════════════════════════════════════

ListManyToManyCursorHandler::ListManyToManyCursorHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::string target_entity,
    std::string pivot_table,
    std::string source_fk_column,
    std::string target_fk_column,
    sea::domain::CursorPagination config,
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper)
    : crud_engine_(std::move(crud_engine))
    , target_entity_(std::move(target_entity))
    , pivot_table_(std::move(pivot_table))
    , source_fk_column_(std::move(source_fk_column))
    , target_fk_column_(std::move(target_fk_column))
    , config_(std::move(config))
    , auth_helper_(std::move(auth_helper))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
ListManyToManyCursorHandler::handle(const seastar::sstring&,
                                    std::unique_ptr<seastar::http::request> req,
                                    std::unique_ptr<seastar::http::reply> rep)
{
    const auto source_id_sstring = std::string(sea::http::utils::strip_leading_slash(req->get_path_param("id")));
    if (source_id_sstring.empty()) {
        co_return bad_request(std::move(rep), "id manquant");
    }
    const std::string source_id{std::string_view(source_id_sstring)};

    auto parsed = sea::http::utils::parse_cursor_query(*req, config_);
    if (!parsed.ok()) {
        co_return bad_request(std::move(rep), parsed.error.value_or("Invalid query"));
    }
    const CursorRequest& request = *parsed.request;

    // 1. Resolution des target_ids via la table pivot
    const auto pivot_records = co_await crud_engine_->list(pivot_table_);
    std::vector<std::string> target_ids;
    target_ids.reserve(pivot_records.size());
    for (const auto& record : pivot_records) {
        const auto src_it = record.find(source_fk_column_);
        if (src_it == record.end()) continue;
        if (!sea::http::utils::dynamic_value_matches_string(src_it->second, source_id)) {
            continue;
        }
        const auto tgt_it = record.find(target_fk_column_);
        if (tgt_it == record.end()) continue;
        const auto target_id = sea::http::utils::dynamic_value_to_string_id(tgt_it->second);
        if (target_id.has_value()) {
            target_ids.push_back(*target_id);
        }
    }

    // 2. Recupere tous les targets et les trie par cursor_field
    std::vector<DynamicRecord> all_targets;
    all_targets.reserve(target_ids.size());
    for (const auto& target_id : target_ids) {
        const auto target_record = co_await crud_engine_->get_by_id(target_entity_, target_id);
        if (target_record.has_value()) {
            all_targets.push_back(*target_record);
        }
    }

    // Tri par cursor_field (comparaison string)
    std::sort(all_targets.begin(), all_targets.end(),
              [&](const DynamicRecord& a, const DynamicRecord& b) {
                  const auto va = extract_cursor_value(a, request.cursor_field);
                  const auto vb = extract_cursor_value(b, request.cursor_field);
                  if (!va.has_value() && !vb.has_value()) return false;
                  if (!va.has_value()) return !request.sort_desc;
                  if (!vb.has_value()) return request.sort_desc;
                  return request.sort_desc ? (*va > *vb) : (*va < *vb);
              });

    // 3. Trouve le point de depart selon 'after'
    std::size_t start = 0;
    if (request.after.has_value()) {
        const std::string& after = *request.after;
        for (std::size_t i = 0; i < all_targets.size(); ++i) {
            const auto v = extract_cursor_value(all_targets[i], request.cursor_field);
            if (!v.has_value()) continue;
            const bool past_cursor =
                request.sort_desc ? (*v < after) : (*v > after);
            if (past_cursor) {
                start = i;
                break;
            }
            if (i == all_targets.size() - 1) {
                start = all_targets.size();
            }
        }
    }

    // 4. Slice limit
    std::vector<DynamicRecord> page_items;
    if (start < all_targets.size() && request.limit > 0) {
        const std::size_t end = std::min(start + request.limit, all_targets.size());
        page_items.reserve(end - start);
        for (std::size_t i = start; i < end; ++i) {
            page_items.push_back(std::move(all_targets[i]));
        }
    }

    // 5. next_cursor si pages restantes
    std::optional<std::string> next_cursor;
    if (start + page_items.size() < all_targets.size() && !page_items.empty()) {
        next_cursor = extract_cursor_value(page_items.back(), request.cursor_field);
    }

    const std::string items_json = sea::http::utils::records_to_json(page_items);
    const std::string abac_filtered = apply_abac_filter(items_json, target_entity_, auth_helper_, *req);
    const std::string envelope = build_cursor_envelope(abac_filtered, request, next_cursor);

    rep->set_status(seastar::http::reply::status_type::ok);
    rep->write_body("application/json", envelope);
    co_return std::move(rep);
}


// ═════════════════════════════════════════════════════════════════════
// GetWithChildrenCursorHandler
// ═════════════════════════════════════════════════════════════════════

GetWithChildrenCursorHandler::GetWithChildrenCursorHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::string parent_entity,
    std::string child_entity,
    std::string fk_column,
    std::string children_key,
    sea::domain::CursorPagination config,
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper)
    : crud_engine_(std::move(crud_engine))
    , parent_entity_(std::move(parent_entity))
    , child_entity_(std::move(child_entity))
    , fk_column_(std::move(fk_column))
    , children_key_(std::move(children_key))
    , config_(std::move(config))
    , auth_helper_(std::move(auth_helper))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
GetWithChildrenCursorHandler::handle(const seastar::sstring&,
                                     std::unique_ptr<seastar::http::request> req,
                                     std::unique_ptr<seastar::http::reply> rep)
{
    const auto id_sstring = std::string(sea::http::utils::strip_leading_slash(req->get_path_param("id")));
    if (id_sstring.empty()) {
        co_return bad_request(std::move(rep), "id manquant");
    }
    const std::string id{std::string_view(id_sstring)};

    auto parsed = sea::http::utils::parse_cursor_query(*req, config_);
    if (!parsed.ok()) {
        co_return bad_request(std::move(rep), parsed.error.value_or("Invalid query"));
    }
    const CursorRequest& request = *parsed.request;

    // 1. Recupere le parent
    auto parent = co_await crud_engine_->get_by_id(parent_entity_, id);
    if (!parent.has_value()) {
        rep->set_status(seastar::http::reply::status_type::not_found);
        rep->write_body("application/json",
                        json{{"error", "parent introuvable"}}.dump());
        co_return std::move(rep);
    }

    const std::string parent_json = sea::http::utils::record_to_json(*parent);

    // 2. ABAC sur le parent
    if (auth_helper_) {
        const auto subject = auth_helper_->build_subject_from_headers(*req);
        const std::string path_str{std::string_view(req->_url)};
        const auto context = auth_helper_->build_context(*req, path_str);

        const auto check = auth_helper_->check_single(
            parent_entity_,
            sea::domain::access_control::CrudOperation::GetById,
            subject,
            parent_json,
            context
            );

        if (!check.allowed) {
            rep->set_status(seastar::http::reply::status_type::forbidden);
            rep->write_body("application/json",
                            json{{"error", "Forbidden"}, {"message", check.reason}}.dump());
            co_return std::move(rep);
        }
    }

    // 3. Cursor sur les enfants + filtre par fk_column
    const CursorResult page = co_await crud_engine_->list_cursor(child_entity_, request);

    std::vector<DynamicRecord> filtered;
    filtered.reserve(page.items.size());
    for (const auto& c : page.items) {
        const auto it = c.find(fk_column_);
        if (it == c.end()) continue;
        const auto fk = sea::http::utils::dynamic_value_to_string_id(it->second);
        if (fk.has_value() && *fk == id) {
            filtered.push_back(c);
        }
    }

    std::optional<std::string> next_cursor;
    if (page.next_cursor.has_value() && !filtered.empty()) {
        next_cursor = extract_cursor_value(filtered.back(), config_.cursor_field);
    }

    const std::string children_items_json = sea::http::utils::records_to_json(filtered);
    const std::string abac_filtered = apply_abac_filter(children_items_json, child_entity_, auth_helper_, *req);
    const std::string children_envelope = build_cursor_envelope(abac_filtered, request, next_cursor);

    // 4. Reponse imbriquee
    std::string result = parent_json;
    if (!result.empty() && result.back() == '}') {
        result.pop_back();
    }
    result += ", \"";
    result += sea::http::utils::json_escape(children_key_);
    result += "\": ";
    result += children_envelope;
    result += "}";

    rep->set_status(seastar::http::reply::status_type::ok);
    rep->write_body("application/json", result);
    co_return std::move(rep);
}

} // namespace sea::http::handlers::pagination
