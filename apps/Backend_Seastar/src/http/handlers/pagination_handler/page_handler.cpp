#include "page_handler.h"

#include "../access_control/resource_authorization_helper.h"
#include "../../utils/http_utils.h"
#include "../../utils/pagination_query.h"

#include "access_control/crud_operation.h"
#include "runtime/generic_crud_engine.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <utility>

namespace sea::http::handlers::pagination {

namespace {

using json = nlohmann::json;
using sea::infrastructure::runtime::DynamicRecord;
using sea::infrastructure::persistence::PageRequest;
using sea::infrastructure::persistence::PageResult;

// ─────────────────────────────────────────────────────────────────────
// Helper interne : construit l'enveloppe JSON page-based.
//
// La cle "sort" est presente uniquement si un tri a ete applique.
// total_pages = ceil(total / page_size), au moins 1 si total > 0.
// ─────────────────────────────────────────────────────────────────────
[[nodiscard]] std::string build_page_envelope(
    const std::string& items_json,
    const PageRequest& request,
    std::size_t total)
{
    std::size_t total_pages = 0;
    if (request.page_size > 0) {
        total_pages = (total + request.page_size - 1) / request.page_size;
    }
    if (total > 0 && total_pages == 0) total_pages = 1;

    std::ostringstream oss;
    oss << "{\"items\":" << items_json
        << ",\"page\":" << request.page
        << ",\"page_size\":" << request.page_size
        << ",\"total\":" << total
        << ",\"total_pages\":" << total_pages;

    if (request.sort_field.has_value()) {
        oss << ",\"sort\":\""
            << sea::http::utils::json_escape(*request.sort_field)
            << ":" << (request.sort_desc ? "desc" : "asc")
            << "\"";
    }

    oss << "}";
    return oss.str();
}

// ─────────────────────────────────────────────────────────────────────
// Helper interne : reply 400 Bad Request avec message d'erreur.
// ─────────────────────────────────────────────────────────────────────
[[nodiscard]] std::unique_ptr<seastar::http::reply> bad_request(
    std::unique_ptr<seastar::http::reply> rep,
    const std::string& message)
{
    rep->set_status(seastar::http::reply::status_type::bad_request);
    rep->write_body("application/json",
                    json{{"error", "Bad Request"}, {"message", message}}.dump());
    return rep;
}

// ─────────────────────────────────────────────────────────────────────
// Helper interne : applique le filtre ABAC sur la liste de records,
// puis renvoie le JSON filtre.
//
// LIMITATION CONNUE (Etape 4.2) :
// Le filtre ABAC s'applique APRES la pagination, donc :
// - Les pages peuvent contenir moins d'items que page_size
// - Le 'total' compte des records peut-etre non accessibles
//
// C'est acceptable pour ABAC subject-only (qui se resout au middleware
// avant ce handler) et pour les cas simples. Pour ABAC resource-aware
// strict, prevoir un filtrage en amont (cout : full scan).
// ─────────────────────────────────────────────────────────────────────
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
    const std::string path_str(req._url.data(), req._url.size());
    const auto context = auth_helper->build_context(req, path_str);

    return auth_helper->filter_collection(
        entity_name,
        sea::domain::access_control::CrudOperation::List,
        subject,
        records_json,
        context
        );
}

} // namespace anonyme


// ═════════════════════════════════════════════════════════════════════
// ListPageHandler
// ═════════════════════════════════════════════════════════════════════

ListPageHandler::ListPageHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::string entity_name,
    sea::domain::PagePagination config,
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper)
    : crud_engine_(std::move(crud_engine))
    , entity_name_(std::move(entity_name))
    , config_(std::move(config))
    , auth_helper_(std::move(auth_helper))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
ListPageHandler::handle(const seastar::sstring&,
                        std::unique_ptr<seastar::http::request> req,
                        std::unique_ptr<seastar::http::reply> rep)
{
    auto parsed = sea::http::utils::parse_page_query(*req, config_);
    if (!parsed.ok()) {
        co_return bad_request(std::move(rep), parsed.error.value_or("Invalid query"));
    }

    const PageRequest& request = *parsed.request;
    const PageResult page = co_await crud_engine_->list_page(entity_name_, request);

    const std::string items_json = sea::http::utils::records_to_json(page.items);
    const std::string filtered = apply_abac_filter(items_json, entity_name_, auth_helper_, *req);
    const std::string envelope = build_page_envelope(filtered, request, page.total);

    rep->set_status(seastar::http::reply::status_type::ok);
    rep->write_body("application/json", envelope);
    co_return std::move(rep);
}


// ═════════════════════════════════════════════════════════════════════
// ListByFkPageHandler
//
// Strategie MVP : on appelle list_page sur l'entite enfant (qui pagine
// cote DB), puis on filtre cote handler par fk_column.
//
// Limitation : le filtre est applique APRES le slice DB, donc le 'total'
// renvoye = total global de l'entite, pas total filtre. C'est une
// limitation MVP : pour un total correct, il faudrait ajouter un filtre
// WHERE dans list_page (a faire dans une iteration future).
// ═════════════════════════════════════════════════════════════════════

ListByFkPageHandler::ListByFkPageHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::string child_entity,
    std::string fk_column,
    sea::domain::PagePagination config,
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper)
    : crud_engine_(std::move(crud_engine))
    , child_entity_(std::move(child_entity))
    , fk_column_(std::move(fk_column))
    , config_(std::move(config))
    , auth_helper_(std::move(auth_helper))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
ListByFkPageHandler::handle(const seastar::sstring&,
                            std::unique_ptr<seastar::http::request> req,
                            std::unique_ptr<seastar::http::reply> rep)
{
    const auto parent_id_sstring = sea::http::utils::strip_leading_slash(req->get_path_param("id"));
    if (parent_id_sstring.empty()) {
        co_return bad_request(std::move(rep), "id manquant");
    }
    const std::string parent_id(parent_id_sstring.data(), parent_id_sstring.size());

    auto parsed = sea::http::utils::parse_page_query(*req, config_);
    if (!parsed.ok()) {
        co_return bad_request(std::move(rep), parsed.error.value_or("Invalid query"));
    }

    // MVP : on pagine puis on filtre. Le repository ne supporte pas
    // encore le filtrage par FK au niveau SQL (a evoluer plus tard).
    const PageRequest& request = *parsed.request;
    const PageResult page = co_await crud_engine_->list_page(child_entity_, request);

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

    const std::string items_json = sea::http::utils::records_to_json(filtered);
    const std::string abac_filtered = apply_abac_filter(items_json, child_entity_, auth_helper_, *req);
    const std::string envelope = build_page_envelope(abac_filtered, request, page.total);

    rep->set_status(seastar::http::reply::status_type::ok);
    rep->write_body("application/json", envelope);
    co_return std::move(rep);
}


// ═════════════════════════════════════════════════════════════════════
// ListByFkFieldPageHandler
//
// Etapes :
// 1. Resolution du parent par recherche sur search_field (full scan)
// 2. Pagination sur enfants
// 3. Filtrage par fk_column = parent.id
//
// Meme limitation que ListByFkPageHandler concernant le 'total'.
// ═════════════════════════════════════════════════════════════════════

ListByFkFieldPageHandler::ListByFkFieldPageHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::string child_entity,
    std::string parent_entity,
    std::string fk_column,
    std::string search_field,
    sea::domain::PagePagination config,
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
ListByFkFieldPageHandler::handle(const seastar::sstring&,
                                 std::unique_ptr<seastar::http::request> req,
                                 std::unique_ptr<seastar::http::reply> rep)
{
    const auto value_sstring = sea::http::utils::strip_leading_slash(req->get_path_param("value"));
    if (value_sstring.empty()) {
        co_return bad_request(std::move(rep), "value manquant");
    }
    const std::string value(value_sstring.data(), value_sstring.size());

    auto parsed = sea::http::utils::parse_page_query(*req, config_);
    if (!parsed.ok()) {
        co_return bad_request(std::move(rep), parsed.error.value_or("Invalid query"));
    }

    // 1. Resolution du parent (full scan, comme l'existant)
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

    // 2. Pagination sur enfants
    const PageRequest& request = *parsed.request;
    const PageResult page = co_await crud_engine_->list_page(child_entity_, request);

    // 3. Filtrage par fk_column = parent.id
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

    const std::string items_json = sea::http::utils::records_to_json(filtered);
    const std::string abac_filtered = apply_abac_filter(items_json, child_entity_, auth_helper_, *req);
    const std::string envelope = build_page_envelope(abac_filtered, request, page.total);

    rep->set_status(seastar::http::reply::status_type::ok);
    rep->write_body("application/json", envelope);
    co_return std::move(rep);
}


// ═════════════════════════════════════════════════════════════════════
// ListManyToManyPageHandler
//
// Etapes :
// 1. Lit la table pivot (full) pour resoudre les target_ids associes
//    a source_id
// 2. Recupere les targets via get_by_id (1 par 1)
// 3. Pagine en memoire sur la liste resolue (slice + total = nb total
//    de targets lies)
//
// Particularite : ici on NE peut PAS appeler list_page sur l'entite
// target (sinon on perdrait le lien M2M). On pagine donc cote handler
// apres avoir resolu la liste M2M complete.
// ═════════════════════════════════════════════════════════════════════

ListManyToManyPageHandler::ListManyToManyPageHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::string target_entity,
    std::string pivot_table,
    std::string source_fk_column,
    std::string target_fk_column,
    sea::domain::PagePagination config,
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
ListManyToManyPageHandler::handle(const seastar::sstring&,
                                  std::unique_ptr<seastar::http::request> req,
                                  std::unique_ptr<seastar::http::reply> rep)
{
    const auto source_id_sstring = std::string(sea::http::utils::strip_leading_slash(req->get_path_param("id")));
    if (source_id_sstring.empty()) {
        co_return bad_request(std::move(rep), "id manquant");
    }
    const std::string source_id(source_id_sstring.data(), source_id_sstring.size());

    auto parsed = sea::http::utils::parse_page_query(*req, config_);
    if (!parsed.ok()) {
        co_return bad_request(std::move(rep), parsed.error.value_or("Invalid query"));
    }
    const PageRequest& request = *parsed.request;

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

    // 2. Recupere tous les targets lies
    std::vector<DynamicRecord> all_targets;
    all_targets.reserve(target_ids.size());
    for (const auto& target_id : target_ids) {
        const auto target_record = co_await crud_engine_->get_by_id(target_entity_, target_id);
        if (target_record.has_value()) {
            all_targets.push_back(*target_record);
        }
    }

    const std::size_t total = all_targets.size();

    // 3. Pagination en memoire (slice)
    const std::size_t page = request.page > 0 ? request.page : 1;
    const std::size_t offset = (page - 1) * request.page_size;
    std::vector<DynamicRecord> page_items;
    if (offset < all_targets.size() && request.page_size > 0) {
        const std::size_t end = std::min(offset + request.page_size, all_targets.size());
        page_items.reserve(end - offset);
        for (std::size_t i = offset; i < end; ++i) {
            page_items.push_back(std::move(all_targets[i]));
        }
    }

    const std::string items_json = sea::http::utils::records_to_json(page_items);
    const std::string abac_filtered = apply_abac_filter(items_json, target_entity_, auth_helper_, *req);
    const std::string envelope = build_page_envelope(abac_filtered, request, total);

    rep->set_status(seastar::http::reply::status_type::ok);
    rep->write_body("application/json", envelope);
    co_return std::move(rep);
}


// ═════════════════════════════════════════════════════════════════════
// GetWithChildrenPageHandler
//
// Reponse imbriquee (Option alpha) :
//   {
//     "id": "...", "name": "Dept A",   <- parent inchange
//     "users": {                        <- enveloppe sur l'enfant
//       "items": [...],
//       "page": 1, "page_size": 20, "total": 137, "total_pages": 7
//     }
//   }
// ═════════════════════════════════════════════════════════════════════

GetWithChildrenPageHandler::GetWithChildrenPageHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::string parent_entity,
    std::string child_entity,
    std::string fk_column,
    std::string children_key,
    sea::domain::PagePagination config,
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
GetWithChildrenPageHandler::handle(const seastar::sstring&,
                                   std::unique_ptr<seastar::http::request> req,
                                   std::unique_ptr<seastar::http::reply> rep)
{
    const auto id_sstring = std::string(sea::http::utils::strip_leading_slash(req->get_path_param("id")));
    if (id_sstring.empty()) {
        co_return bad_request(std::move(rep), "id manquant");
    }
    const std::string id(id_sstring.data(), id_sstring.size());

    auto parsed = sea::http::utils::parse_page_query(*req, config_);
    if (!parsed.ok()) {
        co_return bad_request(std::move(rep), parsed.error.value_or("Invalid query"));
    }
    const PageRequest& request = *parsed.request;

    // 1. Recupere le parent
    auto parent = co_await crud_engine_->get_by_id(parent_entity_, id);
    if (!parent.has_value()) {
        rep->set_status(seastar::http::reply::status_type::not_found);
        rep->write_body("application/json",
                        json{{"error", "parent introuvable"}}.dump());
        co_return std::move(rep);
    }

    const std::string parent_json = sea::http::utils::record_to_json(*parent);

    // 2. Check ABAC sur le parent (comme GetWithChildrenHandler)
    if (auth_helper_) {
        const auto subject = auth_helper_->build_subject_from_headers(*req);
        const std::string path_str(req->_url.data(), req->_url.size());
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

    // 3. Page sur les enfants + filtre par fk_column = id
    //    Meme limitation MVP que ListByFkPageHandler sur le 'total'.
    const PageResult page = co_await crud_engine_->list_page(child_entity_, request);

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

    const std::string children_items_json = sea::http::utils::records_to_json(filtered);
    const std::string abac_filtered = apply_abac_filter(children_items_json, child_entity_, auth_helper_, *req);
    const std::string children_envelope = build_page_envelope(abac_filtered, request, page.total);

    // 4. Construit la reponse imbriquee : parent + {children_key: envelope}
    //    Reutilise la technique de GetWithChildrenHandler :
    //    on enleve le '}' final du parent_json, on injecte la cle.
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