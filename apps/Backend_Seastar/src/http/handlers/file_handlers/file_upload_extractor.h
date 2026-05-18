#pragma once

// ─────────────────────────────────────────────────────────────
// FileUploadExtractor
//
// Module transverse entre les handlers HTTP (Create/Update) et
// le FileService. Sa raison d'être : éviter de dupliquer la
// logique d'extraction dans chaque handler.
//
// Responsabilités :
//   1. Détecter le Content-Type (multipart vs JSON+base64)
//   2. Parser le body selon le mode
//   3. Pour chaque champ File de l'entité :
//        - extraire le contenu binaire + metadata (filename, mime)
//        - appeler FileService::upload
//        - substituer dans le DynamicRecord la valeur du champ
//          par l'UUID retourné
//   4. Tracker les UUIDs uploadés pour que le caller puisse :
//        - retain() après commit de l'entité parente
//        - release() automatique en cas de rollback
//
// Le module NE fait PAS :
//   - parser les champs non-File (texte, numérique, etc.) :
//     ça reste le job du JsonRecordParser ou du caller
//   - gérer la transaction SQL : c'est au handler
//   - gérer l'ABAC : c'est au handler
//
// Modes supportés :
//   ┌─────────────────────────────────────────────────────────┐
//   │ multipart/form-data                                     │
//   │   - Body parsé via notre MultipartParser                │
//   │   - Champs texte → ajoutés au DynamicRecord (string)    │
//   │   - Champs file  → upload + UUID dans le record         │
//   ├─────────────────────────────────────────────────────────┤
//   │ application/json                                        │
//   │   - Le body est déjà parsé par le caller (JsonRecord    │
//   │     Parser). On reçoit le DynamicRecord et on cherche   │
//   │     les champs File qui sont des objets avec :          │
//   │       { filename, mime_type, content_base64 }           │
//   │   - On décode et upload, puis on substitue par UUID     │
//   ├─────────────────────────────────────────────────────────┤
//   │ Autre / pas de Content-Type                             │
//   │   - On n'extrait rien : laisse passer, c'est au caller  │
//   │     de produire 400 si l'entité a des champs File       │
//   │     requis non fournis.                                 │
//   └─────────────────────────────────────────────────────────┘
// ─────────────────────────────────────────────────────────────

#include "fileservice.h"
#include "entity.h"
#include "field.h"
#include "../../utils/multipart_parser.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <seastar/core/future.hh>
#include <seastar/http/request.hh>

namespace sea::http::handlers::file_upload {

// ─────────────────────────────────────────────────────────────
// ExtractionResult
//
// Résultat d'une extraction sur une requête.
//   - `uploaded_uuids` : liste des UUIDs créés pendant cette
//     extraction. Le caller DOIT appeler retain() sur chacun
//     APRÈS commit de l'entité parente, ou rollback() sur l'
//     ensemble si l'entité n'a pas pu être créée/modifiée.
//   - `text_parts` : pour les requêtes multipart, les valeurs
//     des champs texte (sous forme string brute). Le caller
//     les injectera dans le record final via JsonRecordParser
//     ou conversion directe selon le champ.
//   - `had_files` : true si au moins un upload a été effectué
//     (utile pour décider du flow côté handler).
// ─────────────────────────────────────────────────────────────
struct ExtractionResult {
    std::vector<std::string> uploaded_uuids;
    std::vector<std::pair<std::string, std::string>> text_parts;
    bool had_files = false;
};

// ─────────────────────────────────────────────────────────────
// FileUploadExtractor
//
// Stateless service injecté avec ses dépendances. Une seule
// instance par service backend, partagée entre tous les handlers.
// ─────────────────────────────────────────────────────────────
class FileUploadExtractor {
public:
    explicit FileUploadExtractor(
        std::shared_ptr<sea::application::FileService> file_service);

    // ─────────────────────────────────────────────────────────
    // is_multipart_request
    //
    // Petit helper qui combine Seastar (req.is_multi_part()) avec
    // notre extract_boundary() pour confirmer que la requête est
    // bien un multipart parsable.
    //
    // Retourne true uniquement si :
    //   - Seastar a classifié la requête comme multipart, ET
    //   - on a pu extraire un boundary valide du Content-Type.
    // ─────────────────────────────────────────────────────────
    [[nodiscard]] static bool
    is_multipart_request(const seastar::http::request& req) noexcept;

    // ─────────────────────────────────────────────────────────
    // extract_from_multipart
    //
    // Cas multipart/form-data. À appeler par les handlers qui
    // détectent un Content-Type multipart.
    //
    // Modifie `record` en place :
    //   - chaque champ File de l'entité ayant un PartFile dans
    //     le body : valeur substituée par l'UUID retourné par
    //     FileService::upload
    //   - chaque champ texte du multipart : ajouté au record
    //     comme string brute
    //
    // Lève sea_errors_handling::StorageException si :
    //   - parsing multipart échoue (body malformé)
    //   - validation FileService échoue (mime, size, extension)
    //   - upload disque ou DB échoue
    //
    // En cas d'exception levée APRÈS qu'un ou plusieurs uploads
    // soient déjà passés (ex: 1er fichier OK, 2e refusé), les
    // UUIDs déjà uploadés sont dans `result.uploaded_uuids` —
    // le handler doit appeler rollback() pour les supprimer.
    // ─────────────────────────────────────────────────────────
    seastar::future<ExtractionResult>
    extract_from_multipart(
        const seastar::http::request& req,
        const std::string& body,
        const sea::domain::Entity& entity,
        sea::infrastructure::runtime::DynamicRecord& record);

    // ─────────────────────────────────────────────────────────
    // extract_from_json_record
    //
    // Cas application/json. À appeler par les handlers APRÈS
    // que le JsonRecordParser ait produit le `record` initial.
    //
    // Pour chaque champ File de l'entité :
    //   - Si la valeur dans le record est une string : on
    //     suppose que c'est déjà un UUID (référence à un
    //     fichier existant). On ne fait RIEN — pas d'upload.
    //   - Si c'est un objet JSON sérialisé avec
    //     { filename, mime_type, content_base64 } : on décode
    //     et on upload.
    //   - Si absent et champ optionnel : OK.
    //   - Si absent et champ required : c'est au caller de
    //     vérifier (le validator l'aura déjà fait).
    //
    // Cette méthode reçoit le `record` DÉJÀ parsé par le caller
    // (le caller a accès au body brut et au parser). Pour les
    // champs File en mode JSON, on s'attend à ce que le caller
    // ait stocké soit :
    //   - une string (UUID référence existante)
    //   - le contenu JSON brut sous forme de string que l'on
    //     parse ici, OU un nlohmann::json dans le DynamicValue
    //     (variant supporte nlohmann::json)
    //
    // Lève StorageException pour les mêmes raisons que multipart.
    // ─────────────────────────────────────────────────────────
    // ─────────────────────────────────────────────────────────
    // upload_single_part
    //
    // Variante "fine-grained" : upload un seul PartFile multipart
    // contre un Field donné, sans modifier de record. Utile aux
    // handlers qui veulent contrôler eux-mêmes la boucle pour
    // tout englober dans une transaction SQL (cf. CreateHandler).
    //
    // Le caller passe le Field récupéré de l'entité (qui DOIT être
    // un is_file_field), et la part multipart contenant le contenu
    // binaire + filename + content_type.
    //
    // Lève sea_errors_handling::StorageException en cas d'échec
    // (validation refusée, écriture disque échouée, INSERT sea_files
    // raté).
    // ─────────────────────────────────────────────────────────
    seastar::future<sea::application::UploadResult>
    upload_single_part(
        const sea::domain::Field& field,
        const sea::http::utils::multipart::PartFile& part);

    seastar::future<ExtractionResult>
    extract_from_json_record(
        const sea::domain::Entity& entity,
        sea::infrastructure::runtime::DynamicRecord& record);

    // ─────────────────────────────────────────────────────────
    // rollback
    //
    // Supprime physiquement les fichiers uploadés ET leur record
    // sea_files. À appeler en cas d'échec d'INSERT/UPDATE de
    // l'entité parente.
    //
    // Comme les fichiers viennent d'être uploadés avec
    // reference_count = 0, on peut les supprimer en force via
    // FileService::release(uuid, OnDeleteFile::Cascade) qui
    // décrémentera à -1, verra ref_count == 0, et nettoiera.
    //
    // Best-effort : si une suppression échoue, on log mais on
    // continue avec les suivantes. Les fichiers non nettoyés
    // resteront orphelins et seront collectés par un job offline.
    // ─────────────────────────────────────────────────────────
    seastar::future<>
    rollback(const ExtractionResult& result);

    // ─────────────────────────────────────────────────────────
    // commit
    //
    // Incrémente reference_count à 1 pour chaque UUID uploadé.
    // À appeler en cas de SUCCÈS d'INSERT/UPDATE de l'entité.
    //
    // Retourne false si l'un des retain a échoué — dans ce cas,
    // le caller devrait considérer la transaction comme suspecte
    // et inspecter (le UUID est valide en sea_files mais pas
    // référencé, c'est-à-dire orphelin malgré le succès).
    // ─────────────────────────────────────────────────────────
    seastar::future<bool>
    commit(const ExtractionResult& result);

    // ─────────────────────────────────────────────────────────
    // release_old_uuid
    //
    // Helper pour les UPDATE qui remplacent un fichier : déréférence
    // l'ancien UUID selon son on_delete config. Façade pure autour
    // de FileService::release pour éviter au handler d'avoir à
    // accéder au file_service directement.
    //
    // Best-effort : retourne true si OK, false en cas d'échec.
    // Le handler peut logger et continuer (ne pas faire échouer
    // l'update qui a déjà réussi côté SQL).
    // ─────────────────────────────────────────────────────────
    seastar::future<bool>
    release_old_uuid(const std::string& uuid,
                     sea::domain::OnDeleteFile rule);

private:
    std::shared_ptr<sea::application::FileService> file_service_;

    // Trouve un champ File dans l'entité par nom. Retourne nullptr
    // si pas trouvé ou si le champ n'est pas un File.
    [[nodiscard]] static const sea::domain::Field*
    find_file_field(const sea::domain::Entity& entity,
                    std::string_view name) noexcept;
};

} // namespace sea::http::handlers::file_upload