#pragma once

#include "pagination.h"

#include <seastar/http/httpd.hh>

#include <memory>
#include <string>

namespace sea::infrastructure::runtime {
class GenericCrudEngine;
}

namespace sea::http::handlers::access_control {
class ResourceAuthorizationHelper;
}

namespace sea::http::handlers::pagination {

// ─────────────────────────────────────────────────────────────────────
// offset_handlers — Mode "offset/limit"
//
// Route format : GET <base>/offset?offset=K&limit=M&sort=field:dir
//
// Reponse (enveloppe) :
//   {
//     "items":  [...],
//     "offset": 0,
//     "limit":  20,
//     "total":  137,
//     "sort":   "created_at:desc"      (omis si pas de tri)
//   }
//
// 5 classes correspondent aux 5 listings paginables :
//   - ListOffsetHandler                : GET /users/offset
//   - ListByFkOffsetHandler            : GET /users/filter/with_X/{id}/offset
//   - ListByFkFieldOffsetHandler       : GET /users/filter/with_X_Y/{value}/offset
//   - ListManyToManyOffsetHandler      : GET /users/filter/with_X/{id}/offset (M2M)
//   - GetWithChildrenOffsetHandler     : GET /Xs_with_Y/{id}/offset (enveloppe imbriquee)
// ─────────────────────────────────────────────────────────────────────


// ─── ListOffsetHandler ───────────────────────────────────────────────
class ListOffsetHandler final : public seastar::httpd::handler_base {
public:
    ListOffsetHandler(
        std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
        std::string entity_name,
        sea::domain::OffsetPagination config,
        std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper = nullptr
        );

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override;

private:
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine_;
    std::string entity_name_;
    sea::domain::OffsetPagination config_;
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper_;
};


// ─── ListByFkOffsetHandler ───────────────────────────────────────────
class ListByFkOffsetHandler final : public seastar::httpd::handler_base {
public:
    ListByFkOffsetHandler(
        std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
        std::string child_entity,
        std::string fk_column,
        sea::domain::OffsetPagination config,
        std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper = nullptr
        );

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override;

private:
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine_;
    std::string child_entity_;
    std::string fk_column_;
    sea::domain::OffsetPagination config_;
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper_;
};


// ─── ListByFkFieldOffsetHandler ──────────────────────────────────────
class ListByFkFieldOffsetHandler final : public seastar::httpd::handler_base {
public:
    ListByFkFieldOffsetHandler(
        std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
        std::string child_entity,
        std::string parent_entity,
        std::string fk_column,
        std::string search_field,
        sea::domain::OffsetPagination config,
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
    sea::domain::OffsetPagination config_;
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper_;
};


// ─── ListManyToManyOffsetHandler ─────────────────────────────────────
class ListManyToManyOffsetHandler final : public seastar::httpd::handler_base {
public:
    ListManyToManyOffsetHandler(
        std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
        std::string target_entity,
        std::string pivot_table,
        std::string source_fk_column,
        std::string target_fk_column,
        sea::domain::OffsetPagination config,
        std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper = nullptr
        );

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override;

private:
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine_;
    std::string target_entity_;
    std::string pivot_table_;
    std::string source_fk_column_;
    std::string target_fk_column_;
    sea::domain::OffsetPagination config_;
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper_;
};


// ─── GetWithChildrenOffsetHandler ────────────────────────────────────
class GetWithChildrenOffsetHandler final : public seastar::httpd::handler_base {
public:
    GetWithChildrenOffsetHandler(
        std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
        std::string parent_entity,
        std::string child_entity,
        std::string fk_column,
        std::string children_key,
        sea::domain::OffsetPagination config,
        std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper = nullptr
        );

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override;

private:
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine_;
    std::string parent_entity_;
    std::string child_entity_;
    std::string fk_column_;
    std::string children_key_;
    sea::domain::OffsetPagination config_;
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper_;
};

} // namespace sea::http::handlers::pagination