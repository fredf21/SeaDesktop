#include "fileservicefactory.h"
#include "storage/filesystem_storage.h"

#include <spdlog/spdlog.h>

#include <utility>

namespace sea::application {

std::optional<FileServiceBundle>
FileServiceFactory::make(
    const sea::domain::Service& service,
    std::shared_ptr<sea::infrastructure::persistence::IGenericRepository> repository,
    std::shared_ptr<IBlockingExecutor> blocking_executor)
{
    auto log = spdlog::get("sea.boot");

    // ─── Activation conditionnelle ──────────────────────────
    // Si le schema n'a aucun champ File, on ne construit RIEN.
    if (!service.has_file_fields()) {
        log->info("FileServiceFactory: service '{}' has no File fields, "
                  "skipping FileService construction",
                  service.name);
        return std::nullopt;
    }

    // ─── Resolution de la StorageConfig ────────────────────
    sea::domain::StorageConfig storage_config;
    if (service.storage.has_value()) {
        storage_config = *service.storage;
        log->info("FileServiceFactory: using YAML storage config "
                  "(backend=Filesystem, root_path='{}')",
                  storage_config.root_path);
    } else {
        storage_config.backend = sea::domain::StorageBackend::Filesystem;
        storage_config.root_path = "./uploads";
        log->warn("FileServiceFactory: service '{}' has File fields but no "
                  "'storage:' block in YAML — using fallback root_path='./uploads'. "
                  "Declare a 'storage:' block to customize.",
                  service.name);
    }

    // ─── Instanciation IFileStorage ────────────────────────
    std::shared_ptr<sea::infrastructure::storage::IFileStorage> storage;
    switch (storage_config.backend) {
    case sea::domain::StorageBackend::Filesystem:
        storage = std::make_shared<
            sea::infrastructure::storage::FilesystemStorage>(storage_config);
        break;
        // case sea::domain::StorageBackend::S3:  // futur
        //     storage = std::make_shared<S3Storage>(storage_config);
        //     break;
    }
    // Pas de default : le parser YAML rejette deja les backends inconnus.

    log->info("FileServiceFactory: IFileStorage ready (root='{}')",
              storage_config.root_path);

    // ─── FileRepository ────────────────────────────────────
    // Partage le meme IGenericRepository que le CrudEngine pour
    // que les transactions englobent bien INSERT entite + INSERT
    // sea_files dans la meme tx SQL.
    auto file_repo = std::make_shared<
        sea::infrastructure::persistence::FileRepository>(repository);

    // ─── FileService ──────────────────────────────────────
    auto file_service = std::make_shared<FileService>(
        std::move(file_repo),
        storage,
        blocking_executor);

    log->info("FileServiceFactory: FileService ready for service '{}'",
              service.name);

    return FileServiceBundle{
        .file_service = file_service,
        .storage      = storage
    };
}

} // namespace sea::application