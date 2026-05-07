#include "list_many_to_many_handler.h"
#include "../access_control/resource_authorization_helper.h"
#include "../../utils/http_utils.h"

#include "access_control/crud_operation.h"
#include "runtime/generic_crud_engine.h"

#include <utility>
#include <vector>

namespace sea::http::handlers::relation {

using sea::infrastructure::runtime::DynamicRecord;

ListManyToManyHandler::ListManyToManyHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::string pivot_table,
    std::string target_entity,
    std::string source_fk_column,
    std::string target_fk_column,
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper)
    : crud_engine_(std::move(crud_engine))
    , pivot_table_(std::move(pivot_table))
    , target_entity_(std::move(target_entity))
    , source_fk_column_(std::move(source_fk_column))
    , target_fk_column_(std::move(target_fk_column))
    , auth_helper_(std::move(auth_helper))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
ListManyToManyHandler::handle(const seastar::sstring&,
                              std::unique_ptr<seastar::http::request> req,
                              std::unique_ptr<seastar::http::reply> rep)
{
    const auto source_id = std::string(req->get_path_param("id"));
    if (source_id.empty()) {
        rep->set_status(seastar::http::reply::status_type::bad_request);
        rep->write_body("application/json", R"({"error":"Parametre 'id' manquant."})");
        co_return std::move(rep);
    }

    const auto pivot_records = co_await crud_engine_->list(pivot_table_);

    std::vector<std::string> target_ids;
    target_ids.reserve(pivot_records.size());

    for (const auto& record : pivot_records) {
        const auto src_it = record.find(source_fk_column_);
        if (src_it == record.end()) {
            continue;
        }

        if (!utils::dynamic_value_matches_string(src_it->second, source_id)) {
            continue;
        }

        const auto tgt_it = record.find(target_fk_column_);
        if (tgt_it == record.end()) {
            continue;
        }

        const auto target_id = utils::dynamic_value_to_string_id(tgt_it->second);
        if (target_id.has_value()) {
            target_ids.push_back(*target_id);
        }
    }

    std::vector<DynamicRecord> results;
    results.reserve(target_ids.size());

    for (const auto& target_id : target_ids) {
        const auto target_record = co_await crud_engine_->get_by_id(target_entity_, target_id);
        if (target_record.has_value()) {
            results.push_back(*target_record);
        }
    }

    std::string final_json = utils::records_to_json(results);

    // filter ABAC silencieux sur la collection (entite cible)
    if (auth_helper_) {
        const auto subject = auth_helper_->build_subject_from_headers(*req);

        const std::string path_str(req->_url.data(), req->_url.size());
        const auto context = auth_helper_->build_context(*req, path_str);

        final_json = auth_helper_->filter_collection(
            target_entity_,
            sea::domain::access_control::CrudOperation::List,
            subject,
            final_json,
            context
            );
    }

    rep->set_status(seastar::http::reply::status_type::ok);
    rep->write_body("application/json", final_json);
    co_return std::move(rep);
}

} // namespace sea::http::handlers::relation