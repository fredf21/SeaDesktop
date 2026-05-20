#pragma once

#include <seastar/http/httpd.hh>

#include <memory>
#include <string>

namespace sea::infrastructure::runtime {
class GenericCrudEngine;
}

namespace sea::http::handlers::access_control {
class ResourceAuthorizationHelper;
}

namespace sea::http::handlers::relation {

// ─────────────────────────────────────────────────────────────────────
// AttachManyToManyHandler
//
// Cree une association dans une table pivot many-to-many.
//
// Route attendue : POST /<source_entity_lower>s/{id}/<relation_name>/{target_id}
//
// Exemple : POST /articles/{id}/tags/{target_id}
//   -> insere (article_id=id, tag_id=target_id) dans article_tags
//
// Comportement :
//   - 404 si la ressource source (id) n'existe pas
//   - 404 si la ressource cible (target_id) n'existe pas
//   - 409 si l'association existe deja
//   - 201 Created en cas de succes
//   - 403 si l'ABAC refuse l'acces a la ressource source ou cible
//
// L'ABAC est verifie sur les deux extremites de la relation (cf.
// Strategie C de SeaDesktop : double check parent + child).
// ─────────────────────────────────────────────────────────────────────
class AttachManyToManyHandler final : public seastar::httpd::handler_base {
public:
    AttachManyToManyHandler(
        std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
        std::string source_entity,      // ex: "Article"
        std::string target_entity,      // ex: "Tag"
        std::string pivot_table,        // ex: "article_tags"
        std::string source_fk_column,   // ex: "article_id"
        std::string target_fk_column,   // ex: "tag_id"
        std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper = nullptr
        );

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override;

private:
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine_;
    std::string source_entity_;
    std::string target_entity_;
    std::string pivot_table_;
    std::string source_fk_column_;
    std::string target_fk_column_;
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper_;
};

} // namespace sea::http::handlers::relation