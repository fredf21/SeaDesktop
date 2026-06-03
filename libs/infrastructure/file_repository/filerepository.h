#pragma once

// ─────────────────────────────────────────────────────────────
// FileRepository
//
// Repository concret pour la table système `sea_files`. Vit dans
// sea_infrastructure car il dialogue directement avec la base de
// donnees via IGenericRepository.
//
// Pas d'interface (pas de YAGNI) — une seule implementation prevue,
// qui s'appuie sur le repository generique deja branche a MySQL ou
// InMemory.
//
// Responsabilites :
//   - Conversion FileMetadata <-> DynamicRecord (encapsule les noms
//     de colonnes constants definis dans SeaFilesTable)
//   - Methodes metier nommees (add_reference, release_reference)
//     plutot que des increment_field bruts
//   - Reste mince : pas de logique metier sur la cascade ou le
//     storage physique — c'est dans FileService (sea_application)
//
// Tout est asynchrone (seastar::future) car IGenericRepository l'est.
// ─────────────────────────────────────────────────────────────

#include "file_metadata.h"
#include "runtime/dynamic_record.h"
#include "persistence/i_generic_repository.h"

#include <memory>
#include <optional>
#include <string>

#include <seastar/core/future.hh>

namespace sea::infrastructure::persistence {

class FileRepository {
public:
    explicit FileRepository(
        std::shared_ptr<IGenericRepository> repo);

    // ─────────────────────────────────────────────────────────
    // Cree un nouveau record dans sea_files.
    //
    // Le caller fournit un FileMetadata complet (id, original_name,
    // mime_type, size_bytes, storage_path). reference_count est
    // initialise a 0 — le caller appellera ensuite add_reference()
    // une fois pour chaque entite qui pointe sur le fichier.
    //
    // @return true si la creation a reussi.
    // ─────────────────────────────────────────────────────────
    seastar::future<bool>
    insert(const sea::domain::FileMetadata& metadata);

    // Lit un record sea_files par son UUID.
    seastar::future<std::optional<sea::domain::FileMetadata>>
    find_by_id(const std::string& uuid);

    // Incremente reference_count de +1 (atomique cote SGBD).
    seastar::future<bool>
    add_reference(const std::string& uuid);

    // Decremente reference_count de -1 (atomique cote SGBD).
    //
    // ATTENTION : cette methode ne declenche PAS la suppression du
    // fichier physique ni du record sea_files lorsque le compteur
    // atteint 0. C'est le role de FileService::release qui orchestre
    // toute la cascade.
    seastar::future<bool>
    release_reference(const std::string& uuid);

    // Décrémente reference_count de 1, mais SEULEMENT si > 0.
    // Atomique côté SGBD : un compteur ne peut jamais passer sous
    // zéro, même sous des release concurrents.
    //
    // @return true si le décrément a eu lieu, false si le compteur
    //         était déjà <= 0 (ou uuid inconnu).
    seastar::future<bool>
    release_reference_if_positive(const std::string& uuid);

    // Supprime le record sea_files par son UUID.
    // Appele par FileService apres que le compteur atteigne 0 et
    // que le fichier physique ait ete supprime.
    //
    // La FK SQL cote entites utilisateur (RESTRICT par defaut)
    // empeche cette operation s'il reste des entites qui pointent
    // sur ce fichier — c'est notre filet de securite.
    seastar::future<bool>
    delete_row(const std::string& uuid);

private:
    std::shared_ptr<IGenericRepository> repo_;

    // Helpers de conversion DynamicRecord <-> FileMetadata.
    [[nodiscard]] static sea::infrastructure::runtime::DynamicRecord
    to_record(const sea::domain::FileMetadata& metadata);

    [[nodiscard]] static std::optional<sea::domain::FileMetadata>
    from_record(const sea::infrastructure::runtime::DynamicRecord& record);
};

} // namespace sea::infrastructure::persistence