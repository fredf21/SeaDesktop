#pragma once
#include <seastar/http/httpd.hh>
#include <memory>
#include <string>

namespace sea::infrastructure::runtime {
class GenericCrudEngine;
class SchemaRuntimeRegistry;
}

// forward declaration
namespace sea::http::handlers::access_control {
class ResourceAuthorizationHelper;
}

// Forward declaration pour la prise en charge des champs File
namespace sea::http::handlers::file_upload {
class FileUploadExtractor;
}

namespace sea::http::handlers::crud {

class DeleteHandler final : public seastar::httpd::handler_base {
public:
    DeleteHandler(
        std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
        std::string entity_name,
        // helper ABAC resource-aware (optionnel)
        std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper = nullptr,
        // Registry pour acceder au schema de l'entite (optionnel mais
        // requis pour activer la gestion des champs File)
        std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry> registry = nullptr,
        // Extractor de fichiers (optionnel). Si nullptr ou si l'entite
        // n'a aucun champ File, le DELETE fonctionne comme avant.
        std::shared_ptr<sea::http::handlers::file_upload::FileUploadExtractor> file_extractor = nullptr
        );

    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring&,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override;

private:
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine_;
    std::string entity_name_;
    std::shared_ptr<sea::http::handlers::access_control::ResourceAuthorizationHelper> auth_helper_;
    std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry> registry_;
    std::shared_ptr<sea::http::handlers::file_upload::FileUploadExtractor> file_extractor_;
};

}