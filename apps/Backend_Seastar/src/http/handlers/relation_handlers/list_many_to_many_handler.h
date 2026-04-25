#pragma once
#include <seastar/http/httpd.hh>
#include <memory>
#include <string>

namespace sea::infrastructure::runtime { class GenericCrudEngine; }

namespace sea::http::handlers::relation {

class ListManyToManyHandler final : public seastar::httpd::handler_base {
public:
    ListManyToManyHandler(std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
                          std::string pivot_table,
                          std::string target_entity,
                          std::string source_fk_column,
                          std::string target_fk_column);

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override;

private:
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine_;
    std::string pivot_table_;
    std::string target_entity_;
    std::string source_fk_column_;
    std::string target_fk_column_;
};

}
