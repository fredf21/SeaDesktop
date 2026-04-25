#pragma once

#include <seastar/http/httpd.hh>

#include <memory>
#include <string>

namespace sea::infrastructure::runtime {
class GenericCrudEngine;
}

namespace sea::http::handlers::relation {

class ListByFkHandler final : public seastar::httpd::handler_base {
public:
    ListByFkHandler(
        std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
        std::string child_entity,
        std::string fk_column);

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override;

private:
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine_;
    std::string child_entity_;
    std::string fk_column_;
};

} // namespace sea::http::handlers::relation
