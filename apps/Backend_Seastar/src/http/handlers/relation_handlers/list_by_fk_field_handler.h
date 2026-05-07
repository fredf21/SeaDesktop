#pragma once

#include <seastar/http/httpd.hh>

#include <memory>
#include <string>

namespace sea::infrastructure::runtime {
class GenericCrudEngine;
}

// ✨ Module 6 : forward declaration
namespace sea::http::handlers::access_control {
class ResourceAuthorizationHelper;
}

namespace sea::http::handlers::relation {

class ListByFkFieldHandler final : public seastar::httpd::handler_base {
public:
    ListByFkFieldHandler(
        std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
        std::string child_entity,
        std::string parent_entity,
        std::string fk_column,
        std::string search_field,
        // helper ABAC resource-aware (optionnel)
        std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper = nullptr
        );

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override;

private:
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine_;
    std::string child_entity_;
    std::string parent_entity_;
    std::string fk_column_;
    std::string search_field_;
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper_;
};

} // namespace sea::http::handlers::relation