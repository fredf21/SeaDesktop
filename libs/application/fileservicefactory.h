#pragma once

// ─────────────────────────────────────────────────────────────
// FileServiceFactory
//
// Vit dans sea_application (a cote de fileservice, start_service_usecase).
//
// Assemble l'arbre de dependances pour la prise en charge des fichiers :
//
//   StorageConfig (YAML ou fallback)
//        |
//        v
//   IFileStorage (FilesystemStorage)   [sea_infrastructure]
//        |
//        +--> FileRepository           [sea_infrastructure]
//        |    (wrappe IGenericRepository)
//        |
//        v
//   FileService                        [sea_application]
//
// NE construit PAS le FileUploadExtractor (qui vit dans apps/Backend_Seastar
// — couche au-dessus). Le main.cpp s'en charge a partir du FileService
// retourne par cette factory.
//
// Activation conditionnelle :
//   - Si service.has_file_fields() == false → retourne std::nullopt.
//     Aucune table sea_files, aucun dossier, rien.
//   - Sinon utilise service.storage si present, ou fallback automatique
//     vers StorageConfig{ Filesystem, "./uploads" } sinon.
// ─────────────────────────────────────────────────────────────

#include "fileservice.h"
#include "persistence/i_generic_repository.h"
#include "storage/i_file_storage.h"
#include "thread_pool_execution/i_blocking_executor.h"
#include "service.h"

#include <memory>
#include <optional>

namespace sea::application {

// Resultat d'une instanciation reussie.
// Le caller (main.cpp) utilisera ces shared_ptr pour :
//   - construire le FileUploadExtractor (apps/) a partir du FileService
//   - remplir MiddlewareContext.file_service
//   - garder une reference vivante sur le storage
struct FileServiceBundle {
    std::shared_ptr<FileService> file_service;
    std::shared_ptr<sea::infrastructure::storage::IFileStorage> storage;
};

class FileServiceFactory {
public:
    // Construit l'arbre file si necessaire.
    //
    // @param service           Le Service charge depuis le YAML.
    //                          Lit service.storage et service.has_file_fields().
    // @param repository        Le IGenericRepository deja construit (partage
    //                          avec le CrudEngine pour utiliser la meme
    //                          connexion MySQL et donc les memes transactions).
    // @param blocking_executor Pour wrapper les ops sync (storage I/O, etc.)
    //                          hors du reactor Seastar.
    //
    // @return  - std::nullopt si le schema n'a aucun champ File.
    //          - FileServiceBundle peuple sinon.
    //
    // Leve sea_errors_handling::StorageException si :
    //   - la racine du storage ne peut pas etre creee/accedee
    //   - le backend declare est non supporte
    //
    // La factory NE cree PAS la table sea_files (job du bootstrapper,
    // appele independamment en amont avec le meme check has_file_fields).
    [[nodiscard]] static std::optional<FileServiceBundle>
    make(const sea::domain::Service& service,
         std::shared_ptr<sea::infrastructure::persistence::IGenericRepository> repository,
         std::shared_ptr<IBlockingExecutor> blocking_executor);
};

} // namespace sea::application