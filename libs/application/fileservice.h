#pragma once

// ─────────────────────────────────────────────────────────────
// FileService
//
// Service applicatif (couche sea::application) qui orchestre le
// cycle de vie complet d'un fichier upload :
//
//   ┌──────────────┐    ┌──────────────────┐    ┌────────────────┐
//   │  HTTP        │ →  │  FileService     │ →  │  IFileStorage  │
//   │  handler     │    │  (orchestration) │    │  (disque)      │
//   └──────────────┘    │                  │ →  ┌────────────────┐
//                       │                  │    │  FileRepository│
//                       └──────────────────┘    │  (sea_files)   │
//                                               └────────────────┘
//
// Responsabilites :
//   - Validation amont (size, mime, extension) contre la
//     FileFieldConfig du champ declare en YAML
//   - Generation de l'UUID v4 et du chemin storage cible
//   - Ecriture coordonnee storage + sea_files dans la meme
//     transaction (atomicite)
//   - Reference counting (retain/release) avec cascade selon
//     OnDeleteFile
//   - Lecture (download) : retourne contenu + metadata pour que
//     le handler HTTP fixe Content-Type/Content-Disposition
//
// Tout est asynchrone : les handlers HTTP appellent ces methodes
// dans le reactor Seastar, le service delegue les operations
// bloquantes (disque, DB) au IBlockingExecutor via FileRepository
// et IFileStorage.
// ─────────────────────────────────────────────────────────────

#include "file_field_config.h"
#include "file_repository/filerepository.h"
#include "file_field_config.h"
#include "file_metadata.h"
#include "storage/i_file_storage.h"
#include "thread_pool_execution/i_blocking_executor.h"

#include <memory>
#include <optional>
#include <string>

#include <seastar/core/future.hh>

namespace sea::application {

// ─────────────────────────────────────────────────────────────
// Resultat d'un upload reussi.
// Le handler HTTP utilisera l'UUID pour le serialiser dans le JSON
// renvoye au client (la colonne FK de l'entite contient cet UUID).
// ─────────────────────────────────────────────────────────────
struct UploadResult {
    std::string uuid;            // identifiant a stocker dans la FK
    std::string storage_path;    // chemin RELATIF (pour debug/logs)
    std::size_t size_bytes;
    std::string mime_type;
};

// ─────────────────────────────────────────────────────────────
// Resultat d'une operation de validation amont.
// Si `accepted == false`, `error_message` contient le detail
// (utilise par le handler pour produire un 400 Bad Request).
// ─────────────────────────────────────────────────────────────
struct UploadValidationResult {
    bool accepted = true;
    std::string error_message;
};

class FileService {
public:
    FileService(
        std::shared_ptr<sea::infrastructure::persistence::FileRepository> repo,
        std::shared_ptr<sea::infrastructure::storage::IFileStorage> storage,
        std::shared_ptr<IBlockingExecutor> blocking_executor);

    // ─────────────────────────────────────────────────────────
    // Valide un upload contre la FileFieldConfig.
    //
    // A appeler PAR LE HANDLER avant store(), pour pouvoir
    // retourner un 400 Bad Request explicite avant de reserver
    // de l'espace disque inutilement.
    //
    // Verifie :
    //   - size <= config.max_size_bytes
    //   - mime_type ∈ config.allowed_mime_types (si liste non vide)
    //   - extension ∈ config.allowed_extensions (si liste non vide)
    //
    // L'extension est deduite du `original_name` (derniere partie
    // apres le dernier '.'). Insensible a la casse.
    // ─────────────────────────────────────────────────────────
    [[nodiscard]] static UploadValidationResult
    validate_upload(const sea::domain::FileFieldConfig& config,
                    const std::string& original_name,
                    const std::string& mime_type,
                    std::size_t content_size);

    // ─────────────────────────────────────────────────────────
    // Upload : valide, genere UUID, ecrit sur disque, insere dans
    // sea_files avec reference_count = 0.
    //
    // ATTENTION : reference_count est a 0 apres cet appel. Le
    // handler doit appeler retain() une fois l'entite parente
    // creee/mise a jour avec succes. Si l'insert de l'entite
    // echoue, le fichier sera orphelin (reference_count == 0)
    // et sera GC par un job offline.
    //
    // Leve sea_errors_handling::ErrorException si :
    //   - validation echoue (StorageException avec message clair)
    //   - ecriture disque echoue
    //   - insert SQL echoue (rollback storage si possible)
    // ─────────────────────────────────────────────────────────
    seastar::future<UploadResult>
    upload(const sea::domain::FileFieldConfig& config,
           const std::string& original_name,
           const std::string& mime_type,
           std::string content);

    // ─────────────────────────────────────────────────────────
    // Recupere le contenu binaire d'un fichier par son UUID.
    //
    // Retourne le couple (metadata, content) : le handler s'en
    // sert pour fixer Content-Type (depuis mime_type) et
    // Content-Disposition (depuis original_name).
    //
    // Si l'UUID est inconnu, retourne nullopt — le handler
    // produira un 404.
    // ─────────────────────────────────────────────────────────
    struct DownloadResult {
        sea::domain::FileMetadata metadata;
        std::string content;
    };

    seastar::future<std::optional<DownloadResult>>
    download(const std::string& uuid);

    // ─────────────────────────────────────────────────────────
    // Incremente le reference_count.
    //
    // Appele par les handlers HTTP de creation / update qui
    // viennent d'attacher un UUID a un champ d'entite. Si l'UUID
    // est inconnu, retourne false (le handler doit alors faire
    // un rollback / renvoyer 400).
    // ─────────────────────────────────────────────────────────
    seastar::future<bool>
    retain(const std::string& uuid);

    // ─────────────────────────────────────────────────────────
    // Decremente le reference_count et applique le on_delete
    // config selon le resultat :
    //
    //   - OnDeleteFile::Cascade  : si reference_count atteint 0,
    //     supprime le record sea_files ET le fichier physique.
    //
    //   - OnDeleteFile::SetNull  : decremente le compteur, mais
    //     ne supprime PAS le fichier meme a 0. Le cleanup est
    //     delegue a un job offline.
    //
    //   - OnDeleteFile::Restrict : ne devrait JAMAIS arriver ici.
    //     Le handler de DELETE doit refuser en amont si une entite
    //     reference encore le fichier (cf. DeleteHandler).
    //     Si appele par erreur, on log un warning et on traite
    //     comme SetNull (preserve le fichier — fail-safe).
    //
    // @return true si le dereferencement a reussi (que le fichier
    //         soit supprime ou non).
    // ─────────────────────────────────────────────────────────
    seastar::future<bool>
    release(const std::string& uuid,
            sea::domain::OnDeleteFile rule);

private:
    std::shared_ptr<sea::infrastructure::persistence::FileRepository> repo_;
    std::shared_ptr<sea::infrastructure::storage::IFileStorage> storage_;
    std::shared_ptr<IBlockingExecutor> blocking_executor_;

    // Helper : genere le chemin storage cible a partir du
    // storage_path du champ + UUID + extension.
    // ex: ("users/avatars", "550e...", "me.png") -> "users/avatars/550e...png"
    [[nodiscard]] static std::string
    build_target_path(const std::string& storage_path,
                      const std::string& uuid,
                      const std::string& original_name);

    // Generation d'UUID v4 interne au FileService.
    // Pas de dependance vers sea::http::utils::generate_uuid (couplage
    // transverse couches a eviter cf. discussion architecture).
    [[nodiscard]] static std::string generate_uuid();
};

} // namespace sea::application