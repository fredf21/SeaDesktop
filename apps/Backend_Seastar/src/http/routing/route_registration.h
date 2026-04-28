#ifndef ROUTE_REGISTRATION_H
#define ROUTE_REGISTRATION_H

#include "route_generator.h"
#include "service.h"
#include "../middlewares/rate_limit_store.h"

#include <seastar/core/sharded.hh>
#include <seastar/http/httpd.hh>

#include <memory>
#include <string>
#include <vector>
#include "thread_pool_execution/i_blocking_executor.h"

namespace sea::infrastructure::runtime {
class GenericCrudEngine;
class SchemaRuntimeRegistry;
}

namespace sea::application {
class AuthService;
}

namespace sea::http::routing {

// Contexte regroupant tout ce dont les middlewares ont besoin
struct MiddlewareContext {
    const sea::domain::Service& service;
    std::shared_ptr<sea::application::AuthService> auth_service;
    seastar::sharded<sea::http::middlewares::RateLimitStore>* rate_limit_store;
    std::shared_ptr<IBlockingExecutor> blocking_executor;
};

// Wrap un handler avec toute la stack de middlewares.
// Utilise context.service.security pour les configs.
std::unique_ptr<seastar::httpd::handler_base> wrap_with_middlewares(
    std::unique_ptr<seastar::httpd::handler_base> handler,
    bool requires_auth,
    const MiddlewareContext& context
    );

// Routes CRUD
void register_collection_route(
    seastar::httpd::routes& routes,
    const sea::application::RouteDefinition& route,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry>& registry,
    const MiddlewareContext& context
    );

void register_item_route(
    seastar::httpd::routes& routes,
    const sea::application::RouteDefinition& route,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry>& registry,
    const MiddlewareContext& context
    );

// Routes relationnelles
void register_has_many_routes(
    seastar::httpd::routes& routes,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const MiddlewareContext& context
    );

void register_has_one_routes(
    seastar::httpd::routes& routes,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const MiddlewareContext& context
    );

void register_many_to_many_routes(
    seastar::httpd::routes& routes,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const MiddlewareContext& context
    );

// Logging
void log_route_definitions(
    const std::string& service_name,
    const std::vector<sea::application::RouteDefinition>& route_definitions
    );

} // namespace sea::http::routing

#endif // ROUTE_REGISTRATION_H