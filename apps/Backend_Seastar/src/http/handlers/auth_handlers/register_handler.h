#pragma once

#include <seastar/http/httpd.hh>
#include <memory>
#include <string>

namespace sea::application { class AuthService; }
namespace sea::infrastructure::runtime {
class GenericCrudEngine;
class SchemaRuntimeRegistry;
}

namespace sea::domain {
enum class DatabaseType;
}

namespace sea::http::handlers::auth {

class RegisterHandler final : public seastar::httpd::handler_base {
public:
    RegisterHandler(
        std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
        std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry> registry,
        std::shared_ptr<sea::application::AuthService> auth_service,
        sea::domain::DatabaseType db_type);

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override;

private:
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine_;
    std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry> registry_;
    std::shared_ptr<sea::application::AuthService> auth_service_;
    sea::domain::DatabaseType db_type_;
};

}
