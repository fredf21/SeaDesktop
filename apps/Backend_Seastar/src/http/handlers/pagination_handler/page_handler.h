#pragma once

#include "pagination.h"

#include <seastar/http/httpd.hh>

#include <memory>
#include <string>

namespace sea::infrastructure::runtime {
class GenericCrudEngine;
}

// forward declaration
namespace sea::http::handlers::access_control {
class ResourceAuthorizationHelper;
}

namespace sea::http::handlers::pagination {

// ─────────────────────────────────────────────────────────────────────
// page_handlers — Mode "page-based"
//
// Route format : GET <base>/page?page=N&page_size=M&sort=field:dir
//
// Reponse (enveloppe) :
//   {
//     "items":       [...],
//     "page":        1,
//     "page_size":   20,
//     "total":       137,
//     "total_pages": 7,
//     "sort":        "created_at:desc"     (omis si pas de tri)
//   }
//
// 5 classes correspondent aux 5 listings paginables :
//   - ListPageHandler                : GET /users/page
//   - ListByFkPageHandler            : GET /users/filter/with_X/{id}/page
//   - ListByFkFieldPageHandler       : GET /users/filter/with_X_Y/{value}/page
//   - ListManyToManyPageHandler      : GET /users/filter/with_X/{id}/page (M2M)
//   - GetWithChildrenPageHandler     : GET /Xs_with_Y/{id}/page (enveloppe imbriquee)
//
// Chaque handler :
//   1. Parse + valide les query params via pagination_query::parse_page_query
//   2. Appelle GenericCrudEngine::list_page (passthrough vers repo)
//   3. Pour les routes filtrees, applique le filtre cote handler
//      (limitation MVP : ABAC + pagination, voir commentaire en .cpp)
//   4. Construit l'enveloppe JSON et renvoie 200 OK
// ─────────────────────────────────────────────────────────────────────


// ─── ListPageHandler ─────────────────────────────────────────────────
// Route : GET /<entity>s/page?page=...&page_size=...&sort=...
class ListPageHandler final : public seastar::httpd::handler_base {
public:
    ListPageHandler(
        std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
        std::string entity_name,
        sea::domain::PagePagination config,
        std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper = nullptr
        );

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override;

private:
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine_;
    std::string entity_name_;
    sea::domain::PagePagination config_;
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper_;
};


// ─── ListByFkPageHandler ─────────────────────────────────────────────
// Route : GET /<children>/filter/with_<parent>/{id}/page?page=...
class ListByFkPageHandler final : public seastar::httpd::handler_base {
public:
    ListByFkPageHandler(
        std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
        std::string child_entity,
        std::string fk_column,
        sea::domain::PagePagination config,
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
    sea::domain::PagePagination config_;
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper_;
};


// ─── ListByFkFieldPageHandler ────────────────────────────────────────
// Route : GET /<children>/filter/with_<parent>_<field>/{value}/page?page=...
class ListByFkFieldPageHandler final : public seastar::httpd::handler_base {
public:
    ListByFkFieldPageHandler(
        std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
        std::string child_entity,
        std::string parent_entity,
        std::string fk_column,
        std::string search_field,
        sea::domain::PagePagination config,
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
    sea::domain::PagePagination config_;
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper_;
};


// ─── ListManyToManyPageHandler ───────────────────────────────────────
// Route : GET /<target>/filter/with_<source>/{id}/page?page=...
class ListManyToManyPageHandler final : public seastar::httpd::handler_base {
public:
    ListManyToManyPageHandler(
        std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
        std::string target_entity,
        std::string pivot_table,
        std::string source_fk_column,
        std::string target_fk_column,
        sea::domain::PagePagination config,
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
    sea::domain::PagePagination config_;
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper_;
};


// ─── GetWithChildrenPageHandler ──────────────────────────────────────
// Route : GET /<parent>s_with_<children>/{id}/page?page=...
//
// Particularite : renvoie le parent + enveloppe imbriquee sur la propriete
// 'children_key' :
//   {
//     "id": "...", "name": "Dept A",
//     "users": { "items": [...], "page": 1, ..., "total_pages": 7 }
//   }
class GetWithChildrenPageHandler final : public seastar::httpd::handler_base {
public:
    GetWithChildrenPageHandler(
        std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
        std::string parent_entity,
        std::string child_entity,
        std::string fk_column,
        std::string children_key,
        sea::domain::PagePagination config,
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
    sea::domain::PagePagination config_;
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper_;
};

} // namespace sea::http::handlers::pagination