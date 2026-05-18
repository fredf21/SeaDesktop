#pragma once

// ─────────────────────────────────────────────────────────────
// FileDownloadByFieldHandler
//
// Handler pour la route GET /<entity>/{id}/<field> qui renvoie
// le contenu binaire d'un fichier référencé par un champ File
// d'une entité.
//
// Exemple : GET /users/123/avatar
//   - Charge le User d'id 123
//   - Applique l'ABAC Read sur User (le subject doit pouvoir
//     lire ce User)
//   - Extrait user.avatar (UUID v4)
//   - Récupère le contenu via FileService::download(uuid)
//   - Renvoie avec Content-Type (mime stocké) et
//     Content-Disposition (original_name stocké)
//
// SÉCURITÉ - héritage ABAC :
//   - L'autorisation est entièrement déléguée à la policy de
//     l'entité parente. Si le subject peut lire le User
//     (CrudOperation::Read), il peut lire son avatar.
//   - Aucune route /files/{uuid} système n'est exposée (cf.
//     décisions de design Étape 7.5).
//
// Réponses possibles :
//   - 200 OK avec body binaire + headers
//   - 400 Bad Request : params manquants
//   - 403 Forbidden : ABAC refuse la lecture
//   - 404 Not Found :
//       * entité inconnue
//       * record introuvable
//       * champ inconnu / pas un File
//       * champ vide (pas de fichier attaché)
//       * UUID inconnu dans sea_files (corruption FK)
//       * fichier physique manquant sur disque
//   - 500 Internal Server Error : erreur I/O autre
// ─────────────────────────────────────────────────────────────

#include <seastar/http/httpd.hh>

#include <memory>
#include <string>

namespace sea::infrastructure::runtime {
class SchemaRuntimeRegistry;
class GenericCrudEngine;
}

namespace sea::http::handlers::access_control {
class ResourceAuthorizationHelper;
}

namespace sea::application {
class FileService;
}

namespace sea::http::handlers::files {

class FileDownloadByFieldHandler final : public seastar::httpd::handler_base {
public:
    FileDownloadByFieldHandler(
        std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
        std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry> registry,
        std::shared_ptr<sea::application::FileService> file_service,
        std::string entity_name,
        std::string field_name,
        // Helper ABAC resource-aware (optionnel). Si nullptr et que le
        // service n'a pas d'auth globale, l'accès est libre - cas qui
        // ne devrait pas arriver pour des champs File en pratique.
        std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper = nullptr
        );

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override;

private:
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine_;
    std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry> registry_;
    std::shared_ptr<sea::application::FileService> file_service_;
    std::string entity_name_;
    std::string field_name_;
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper_;
};

} // namespace sea::http::handlers::files