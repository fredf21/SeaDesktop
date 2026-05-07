#pragma once
#include <seastar/http/httpd.hh>
#include <memory>
#include <string>

namespace sea::infrastructure::runtime { class GenericCrudEngine; }

// forward declaration
namespace sea::http::handlers::access_control {
class ResourceAuthorizationHelper;
}

namespace sea::http::handlers::crud {

class ListHandler final : public seastar::httpd::handler_base {
public:
    ListHandler(
        std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
        std::string entity_name,
        // helper ABAC resource-aware (optionnel : peut etre nullptr
        //  si l'authorization n'est pas activee)
        std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper = nullptr
        );

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request>,
           std::unique_ptr<seastar::http::reply> rep) override;

private:
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine_;
    std::string entity_name_;
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper_;
};

}