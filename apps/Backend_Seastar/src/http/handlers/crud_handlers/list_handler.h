#pragma once
#include <seastar/http/httpd.hh>
#include <memory>
#include <string>

namespace sea::infrastructure::runtime { class GenericCrudEngine; }

namespace sea::http::handlers::crud {

class ListHandler final : public seastar::httpd::handler_base {
public:
    ListHandler(std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
                std::string entity_name);

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request>,
           std::unique_ptr<seastar::http::reply> rep) override;

private:
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine_;
    std::string entity_name_;
};

}
