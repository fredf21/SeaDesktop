#include "offset_handlers.h"

#include "../access_control/resource_authorization_helper.h"
#include "../../utils/http_utils.h"
#include "../../utils/pagination_query.h"
#include "../../errors/error_response_factory.h"

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
using sea::infrastructure::persistence::OffsetRequest;
using sea::infrastructure::persistence::OffsetResult;
namespace errors = sea::http::errors;
using Status = seastar::http::reply::status_type;

// ─────────────────────────────────────────────────────────────────────
// Helper interne : construit l'enveloppe JSON offset/limit.
//
// Pas de total_pages ici (c'est specifique au mode page-based).
// La cle "sort" est presente uniquement si un tri a ete applique.
// ─────────────────────────────────────────────────────────────────────
[[nodiscard]] std::string build_offset_envelope(
    const std::string& items_json,
    const OffsetRequest& request,
    std::size_t total)
{
    std::ostringstream oss;
    oss << "{\"items\":" << items_json
        << ",\"offset\":" << request.offset
        << ",\"limit\":" << request.limit
        << ",\"total\":" << total;

    if (request.sort_field.has_value()) {
        oss << ",\"sort\":\""
            << sea::http::utils::json_escape(*request.sort_field)
            << ":" << (request.sort_desc ? "desc" : "asc")
            << "\"";
    }

    oss << "}";
    return oss.str();
}

// Helper : produit une reponse 400 standardisee via la factory.
// Le parametre `rep` n'est plus utilise mais on garde la signature
// pour minimiser les changements aux call sites.
[[nodiscard]] std::unique_ptr<seastar::http::reply> bad_request(
    std::unique_ptr<seastar::http::reply> /*rep*/,
    const std::string& message)
{
    return errors::make_error_reply(
        Status::bad_request, "BAD_REQUEST", message);
}

// Voir page_handlers.cpp pour la justification de cette limitation MVP.
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

} // namespace anonyme


// ═════════════════════════════════════════════════════════════════════
// ListOffsetHandler
// ═════════════════════════════════════════════════════════════════════

ListOffsetHandler::ListOffsetHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::string entity_name,
    sea::domain::OffsetPagination config,
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper)
    : crud_engine_(std::move(crud_engine))
    , entity_name_(std::move(entity_name))
    , config_(std::move(config))
    , auth_helper_(std::move(auth_helper))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
ListOffsetHandler::handle(const seastar::sstring&,
                          std::unique_ptr<seastar::http::request> req,
                          std::unique_ptr<seastar::http::reply> rep)
{
    auto parsed = sea::http::utils::parse_offset_query(*req, config_);
    if (!parsed.ok()) {
        co_return bad_request(std::move(rep), parsed.error.value_or("Invalid query"));
    }

    const OffsetRequest& request = *parsed.request;
    const OffsetResult page = co_await crud_engine_->list_offset(entity_name_, request);

    const std::string items_json = sea::http::utils::records_to_json(page.items);
    const std::string filtered = apply_abac_filter(items_json, entity_name_, auth_helper_, *req);
    const std::string envelope = build_offset_envelope(filtered, request, page.total);

    rep->set_status(Status::ok);
    rep->write_body("application/json", envelope);
    co_return std::move(rep);
}


// ═════════════════════════════════════════════════════════════════════
// ListByFkOffsetHandler
//
// Limitation MVP : filtrage cote handler apres slice DB.
// Voir ListByFkPageHandler pour la justification.
// ═════════════════════════════════════════════════════════════════════

ListByFkOffsetHandler::ListByFkOffsetHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::string child_entity,
    std::string fk_column,
    sea::domain::OffsetPagination config,
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper)
    : crud_engine_(std::move(crud_engine))
    , child_entity_(std::move(child_entity))
    , fk_column_(std::move(fk_column))
    , config_(std::move(config))
    , auth_helper_(std::move(auth_helper))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
ListByFkOffsetHandler::handle(const seastar::sstring&,
                              std::unique_ptr<seastar::http::request> req,
                              std::unique_ptr<seastar::http::reply> rep)
{
    const auto parent_id_sstring = sea::http::utils::strip_leading_slash(req->get_path_param("id"));
    if (parent_id_sstring.empty()) {
        co_return bad_request(std::move(rep), "Parametre 'id' manquant.");
    }
    const std::string parent_id{std::string_view(parent_id_sstring)};

    auto parsed = sea::http::utils::parse_offset_query(*req, config_);
    if (!parsed.ok()) {
        co_return bad_request(std::move(rep), parsed.error.value_or("Invalid query"));
    }

    const OffsetRequest& request = *parsed.request;
    const OffsetResult page = co_await crud_engine_->list_offset(child_entity_, request);

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
    const std::string envelope = build_offset_envelope(abac_filtered, request, page.total);

    rep->set_status(Status::ok);
    rep->write_body("application/json", envelope);
    co_return std::move(rep);
}


// ═════════════════════════════════════════════════════════════════════
// ListByFkFieldOffsetHandler
// ═════════════════════════════════════════════════════════════════════

ListByFkFieldOffsetHandler::ListByFkFieldOffsetHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::string child_entity,
    std::string parent_entity,
    std::string fk_column,
    std::string search_field,
    sea::domain::OffsetPagination config,
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
ListByFkFieldOffsetHandler::handle(const seastar::sstring&,
                                   std::unique_ptr<seastar::http::request> req,
                                   std::unique_ptr<seastar::http::reply> rep)
{
    const auto value_sstring = sea::http::utils::strip_leading_slash(req->get_path_param("value"));
    if (value_sstring.empty()) {
        co_return bad_request(std::move(rep), "Parametre 'value' manquant.");
    }
    const std::string value{std::string_view(value_sstring)};

    auto parsed = sea::http::utils::parse_offset_query(*req, config_);
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
        co_return errors::make_error_reply(
            Status::not_found, "NOT_FOUND",
            "Parent introuvable.");
    }

    // 2. Pagination sur enfants
    const OffsetRequest& request = *parsed.request;
    const OffsetResult page = co_await crud_engine_->list_offset(child_entity_, request);

    // 3. Filtrage
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
    const std::string envelope = build_offset_envelope(abac_filtered, request, page.total);

    rep->set_status(Status::ok);
    rep->write_body("application/json", envelope);
    co_return std::move(rep);
}


// ═════════════════════════════════════════════════════════════════════
// ListManyToManyOffsetHandler
//
// Strategie : on resout d'abord toute la liste M2M, puis on pagine
// en memoire (slice). Voir ListManyToManyPageHandler pour la
// justification.
// ═════════════════════════════════════════════════════════════════════

ListManyToManyOffsetHandler::ListManyToManyOffsetHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::string target_entity,
    std::string pivot_table,
    std::string source_fk_column,
    std::string target_fk_column,
    sea::domain::OffsetPagination config,
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
ListManyToManyOffsetHandler::handle(const seastar::sstring&,
                                    std::unique_ptr<seastar::http::request> req,
                                    std::unique_ptr<seastar::http::reply> rep)
{
    const auto source_id_sstring = sea::http::utils::strip_leading_slash(req->get_path_param("id"));
    if (source_id_sstring.empty()) {
        co_return bad_request(std::move(rep), "Parametre 'id' manquant.");
    }
    const std::string source_id{std::string_view(source_id_sstring)};

    auto parsed = sea::http::utils::parse_offset_query(*req, config_);
    if (!parsed.ok()) {
        co_return bad_request(std::move(rep), parsed.error.value_or("Invalid query"));
    }
    const OffsetRequest& request = *parsed.request;

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

    // 3. Slice offset/limit en memoire
    std::vector<DynamicRecord> page_items;
    if (request.offset < all_targets.size() && request.limit > 0) {
        const std::size_t end = std::min(request.offset + request.limit, all_targets.size());
        page_items.reserve(end - request.offset);
        for (std::size_t i = request.offset; i < end; ++i) {
            page_items.push_back(std::move(all_targets[i]));
        }
    }

    const std::string items_json = sea::http::utils::records_to_json(page_items);
    const std::string abac_filtered = apply_abac_filter(items_json, target_entity_, auth_helper_, *req);
    const std::string envelope = build_offset_envelope(abac_filtered, request, total);

    rep->set_status(Status::ok);
    rep->write_body("application/json", envelope);
    co_return std::move(rep);
}


// ═════════════════════════════════════════════════════════════════════
// GetWithChildrenOffsetHandler
//
// Reponse imbriquee :
//   {
//     "id": "...", "name": "Dept A",
//     "users": { "items": [...], "offset": 0, "limit": 20, "total": 137 }
//   }
// ═════════════════════════════════════════════════════════════════════

GetWithChildrenOffsetHandler::GetWithChildrenOffsetHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::string parent_entity,
    std::string child_entity,
    std::string fk_column,
    std::string children_key,
    sea::domain::OffsetPagination config,
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
GetWithChildrenOffsetHandler::handle(const seastar::sstring&,
                                     std::unique_ptr<seastar::http::request> req,
                                     std::unique_ptr<seastar::http::reply> rep)
{
    const auto id_sstring = sea::http::utils::strip_leading_slash(req->get_path_param("id"));
    if (id_sstring.empty()) {
        co_return bad_request(std::move(rep), "Parametre 'id' manquant.");
    }
    const std::string id{std::string_view(id_sstring)};

    auto parsed = sea::http::utils::parse_offset_query(*req, config_);
    if (!parsed.ok()) {
        co_return bad_request(std::move(rep), parsed.error.value_or("Invalid query"));
    }
    const OffsetRequest& request = *parsed.request;

    // 1. Recupere le parent
    auto parent = co_await crud_engine_->get_by_id(parent_entity_, id);
    if (!parent.has_value()) {
        co_return errors::make_error_reply(
            Status::not_found, "NOT_FOUND",
            "Parent introuvable.");
    }

    const std::string parent_json = sea::http::utils::record_to_json(*parent);

    // 2. Check ABAC sur le parent
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
            co_return errors::make_error_reply(
                Status::forbidden, "AUTHORIZATION_ERROR",
                check.reason.empty()
                    ? "Acces refuse."
                    : check.reason);
        }
    }

    // 3. Pagine les enfants + filtre par fk_column
    const OffsetResult page = co_await crud_engine_->list_offset(child_entity_, request);

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
    const std::string children_envelope = build_offset_envelope(abac_filtered, request, page.total);

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

    rep->set_status(Status::ok);
    rep->write_body("application/json", result);
    co_return std::move(rep);
}

} // namespace sea::http::handlers::pagination