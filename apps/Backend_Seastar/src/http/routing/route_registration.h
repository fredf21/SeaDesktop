#pragma once

#include <seastar/http/httpd.hh>

#include "route_generator.h"

#include <memory>
#include <string>
#include <vector>

namespace sea::application {
class AuthService;
}

namespace sea::domain {
struct Service;
}

namespace sea::infrastructure::runtime {
class GenericCrudEngine;
class SchemaRuntimeRegistry;
}

namespace sea::http::routing {

void register_collection_route(
    seastar::httpd::routes& routes,
    const sea::application::RouteDefinition& route,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry>& registry,
    const sea::domain::Service& service,
    const std::shared_ptr<sea::application::AuthService>& auth_service);

void register_item_route(
    seastar::httpd::routes& routes,
    const sea::application::RouteDefinition& route,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry>& registry,
    const sea::domain::Service& service,
    const std::shared_ptr<sea::application::AuthService>& auth_service);

void register_has_many_routes(
    seastar::httpd::routes& routes,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const sea::domain::Service& service,
    const std::shared_ptr<sea::application::AuthService>& auth_service);

void register_has_one_routes(
    seastar::httpd::routes& routes,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const sea::domain::Service& service,
    const std::shared_ptr<sea::application::AuthService>& auth_service);

void register_many_to_many_routes(
    seastar::httpd::routes& routes,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const sea::domain::Service& service,
    const std::shared_ptr<sea::application::AuthService>& auth_service);

void log_route_definitions(
    const std::string& service_name,
    const std::vector<sea::application::RouteDefinition>& route_definitions);

} // namespace sea::http::routing
