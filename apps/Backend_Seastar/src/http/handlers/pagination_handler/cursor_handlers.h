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
// cursor_handlers — Mode "cursor"
//
// Route format : GET <base>/cursor?after=<token>&limit=M
//
// Particularites :
// - Pas de calcul de total (technique LIMIT+1 cote repository)
// - Pas de query param 'sort' : le tri est FIGE par le YAML
// - Stable face aux insertions concurrentes
//
// Reponse (enveloppe) :
//   {
//     "items":       [...],
//     "limit":       20,
//     "next_cursor": "abc123"        (absent si derniere page)
//   }
//
// 5 classes :
//   - ListCursorHandler                : GET /users/cursor
//   - ListByFkCursorHandler            : GET /users/filter/with_X/{id}/cursor
//   - ListByFkFieldCursorHandler       : GET /users/filter/with_X_Y/{value}/cursor
//   - ListManyToManyCursorHandler      : GET /users/filter/with_X/{id}/cursor (M2M)
//   - GetWithChildrenCursorHandler     : GET /Xs_with_Y/{id}/cursor
// ─────────────────────────────────────────────────────────────────────


// ─── ListCursorHandler ───────────────────────────────────────────────
class ListCursorHandler final : public seastar::httpd::handler_base {
public:
    ListCursorHandler(
        std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
        std::string entity_name,
        sea::domain::CursorPagination config,
        std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper = nullptr
        );

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override;

private:
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine_;
    std::string entity_name_;
    sea::domain::CursorPagination config_;
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper_;
};


// ─── ListByFkCursorHandler ───────────────────────────────────────────
class ListByFkCursorHandler final : public seastar::httpd::handler_base {
public:
    ListByFkCursorHandler(
        std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
        std::string child_entity,
        std::string fk_column,
        sea::domain::CursorPagination config,
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
    sea::domain::CursorPagination config_;
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper_;
};


// ─── ListByFkFieldCursorHandler ──────────────────────────────────────
class ListByFkFieldCursorHandler final : public seastar::httpd::handler_base {
public:
    ListByFkFieldCursorHandler(
        std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
        std::string child_entity,
        std::string parent_entity,
        std::string fk_column,
        std::string search_field,
        sea::domain::CursorPagination config,
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
    sea::domain::CursorPagination config_;
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper_;
};


// ─── ListManyToManyCursorHandler ─────────────────────────────────────
class ListManyToManyCursorHandler final : public seastar::httpd::handler_base {
public:
    ListManyToManyCursorHandler(
        std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
        std::string target_entity,
        std::string pivot_table,
        std::string source_fk_column,
        std::string target_fk_column,
        sea::domain::CursorPagination config,
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
    sea::domain::CursorPagination config_;
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper_;
};


// ─── GetWithChildrenCursorHandler ────────────────────────────────────
class GetWithChildrenCursorHandler final : public seastar::httpd::handler_base {
public:
    GetWithChildrenCursorHandler(
        std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
        std::string parent_entity,
        std::string child_entity,
        std::string fk_column,
        std::string children_key,
        sea::domain::CursorPagination config,
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
    sea::domain::CursorPagination config_;
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper_;
};

} // namespace sea::http::handlers::pagination