#include "attach_many_to_many_handler.h"

#include "../access_control/resource_authorization_helper.h"
#include "../../utils/http_utils.h"
#include "../../errors/error_response_factory.h"

#include "access_control/crud_operation.h"
#include "runtime/generic_crud_engine.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <utility>

namespace sea::http::handlers::relation {

using sea::infrastructure::runtime::DynamicRecord;
using json = nlohmann::json;
namespace errors = sea::http::errors;
using Status = seastar::http::reply::status_type;

AttachManyToManyHandler::AttachManyToManyHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::string source_entity,
    std::string target_entity,
    std::string pivot_table,
    std::string source_fk_column,
    std::string target_fk_column,
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper)
    : crud_engine_(std::move(crud_engine))
    , source_entity_(std::move(source_entity))
    , target_entity_(std::move(target_entity))
    , pivot_table_(std::move(pivot_table))
    , source_fk_column_(std::move(source_fk_column))
    , target_fk_column_(std::move(target_fk_column))
    , auth_helper_(std::move(auth_helper))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
AttachManyToManyHandler::handle(const seastar::sstring&,
                                std::unique_ptr<seastar::http::request> req,
                                std::unique_ptr<seastar::http::reply> rep)
{
    // ─────────────────────────────────────────────────────────
    // 1. Extraction et validation des path params
    // ─────────────────────────────────────────────────────────
    const auto source_id = std::string(sea::http::utils::strip_leading_slash(req->get_path_param("id")));
    const auto target_id = std::string(sea::http::utils::strip_leading_slash(req->get_path_param("target_id")));

    if (source_id.empty() || target_id.empty()) {
        co_return errors::make_error_reply(
            Status::bad_request, "BAD_REQUEST",
            "Parametres 'id' ou 'target_id' manquants.");
    }

    // ─────────────────────────────────────────────────────────
    // 2. Verification de l'existence des ressources
    // ─────────────────────────────────────────────────────────
    const auto source_record = co_await crud_engine_->get_by_id(source_entity_, source_id);
    if (!source_record.has_value()) {
        co_return errors::make_error_reply(
            Status::not_found, "NOT_FOUND",
            "Ressource source introuvable.");
    }

    const auto target_record = co_await crud_engine_->get_by_id(target_entity_, target_id);
    if (!target_record.has_value()) {
        co_return errors::make_error_reply(
            Status::not_found, "NOT_FOUND",
            "Ressource cible introuvable.");
    }

    // ─────────────────────────────────────────────────────────
    // 3. ABAC : verification des autorisations sur les 2 extremites
    // ─────────────────────────────────────────────────────────
    if (auth_helper_) {
        const auto subject = auth_helper_->build_subject_from_headers(*req);

        const std::string path_str(req->_url.data(), req->_url.size());
        const auto context = auth_helper_->build_context(*req, path_str);

        const std::string source_json =
            sea::http::utils::record_to_json(*source_record);

        const auto source_check = auth_helper_->check_single(
            source_entity_,
            sea::domain::access_control::CrudOperation::Update,
            subject,
            source_json,
            context
            );

        if (!source_check.allowed) {
            co_return errors::make_error_reply(
                Status::forbidden, "AUTHORIZATION_ERROR",
                source_check.reason.empty()
                    ? "Acces refuse sur la ressource source."
                    : source_check.reason);
        }

        const std::string target_json =
            sea::http::utils::record_to_json(*target_record);

        const auto target_check = auth_helper_->check_single(
            target_entity_,
            sea::domain::access_control::CrudOperation::Update,
            subject,
            target_json,
            context
            );

        if (!target_check.allowed) {
            co_return errors::make_error_reply(
                Status::forbidden, "AUTHORIZATION_ERROR",
                target_check.reason.empty()
                    ? "Acces refuse sur la ressource cible."
                    : target_check.reason);
        }
    }

    // ─────────────────────────────────────────────────────────
    // 4. Acces direct au repository
    //
    // GenericCrudEngine n'expose pas insert_pivot/pivot_exists ;
    // on passe par get_repository() pour atteindre directement
    // l'IGenericRepository (utilise aussi par le SeedOrchestrator).
    // ─────────────────────────────────────────────────────────
    auto repository = crud_engine_->get_repository();
    if (!repository) {
        spdlog::get("sea.http")->error(
            "AttachManyToManyHandler: repository unavailable");
        co_return errors::make_internal_error_reply();
    }

    // ─────────────────────────────────────────────────────────
    // 5. Verifier qu'aucune association n'existe deja
    // ─────────────────────────────────────────────────────────
    DynamicRecord lookup_values;
    lookup_values[source_fk_column_] = source_id;
    lookup_values[target_fk_column_] = target_id;

    const auto already_exists =
        co_await repository->pivot_exists(pivot_table_, lookup_values);

    if (already_exists) {
        co_return errors::make_error_reply(
            Status::conflict, "CONFLICT",
            "Association deja existante.");
    }

    // ─────────────────────────────────────────────────────────
    // 6. Insertion dans la table pivot
    // ─────────────────────────────────────────────────────────
    DynamicRecord insert_values;
    insert_values[source_fk_column_] = source_id;
    insert_values[target_fk_column_] = target_id;

    const bool inserted =
        co_await repository->insert_pivot(pivot_table_, insert_values);

    if (!inserted) {
        spdlog::get("sea.http")->error(
            "AttachManyToManyHandler: insert_pivot failed for {} {} -> {}",
            pivot_table_, source_id, target_id);

        co_return errors::make_internal_error_reply();
    }

    spdlog::get("sea.http")->info(
        "AttachManyToManyHandler: associated {}/{} <-> {}/{} via {}",
        source_entity_, source_id,
        target_entity_, target_id,
        pivot_table_);

    // ─────────────────────────────────────────────────────────
    // 7. Reponse
    // ─────────────────────────────────────────────────────────
    rep->set_status(Status::created);
    rep->write_body("application/json",
                    json{{"status", "associated"},
                         {"source_id", source_id},
                         {"target_id", target_id}}.dump());
    co_return std::move(rep);
}

} // namespace sea::http::handlers::relation