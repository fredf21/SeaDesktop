#include "list_handler.h"
#include "../access_control/resource_authorization_helper.h"
#include "../../utils/http_utils.h"

#include "access_control/crud_operation.h"
#include "runtime/generic_crud_engine.h"

#include <utility>

namespace sea::http::handlers::crud {

ListHandler::ListHandler(
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::string entity_name,
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper)
    : crud_engine_(std::move(crud_engine))
    , entity_name_(std::move(entity_name))
    , auth_helper_(std::move(auth_helper))
{
}

seastar::future<std::unique_ptr<seastar::http::reply>>
ListHandler::handle(const seastar::sstring&,
                    std::unique_ptr<seastar::http::request> req,
                    std::unique_ptr<seastar::http::reply> rep)
{
    const auto records = co_await crud_engine_->list(entity_name_);
    const std::string records_json = sea::http::utils::records_to_json(records);

    // filtrage silencieux selon les regles ABAC
    // - Si auth_helper est null (autorisation desactivee) → retour tel quel
    // - Si pas de regle resource-aware → retour tel quel (deja decide par Module 5)
    // - Sinon → filtre les records dont l'evaluation refuse
    std::string final_json = records_json;

    if (auth_helper_ && req) {
        const auto subject = auth_helper_->build_subject_from_headers(*req);

        const std::string path_str(req->_url.data(), req->_url.size());
        const auto context = auth_helper_->build_context(*req, path_str);

        final_json = auth_helper_->filter_collection(
            entity_name_,
            sea::domain::access_control::CrudOperation::List,
            subject,
            records_json,
            context
            );
    }

    rep->set_status(seastar::http::reply::status_type::ok);
    rep->write_body("application/json", final_json);
    co_return std::move(rep);
}

} // namespace sea::http::handlers::crud