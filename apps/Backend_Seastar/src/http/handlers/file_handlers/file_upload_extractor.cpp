#include "file_upload_extractor.h"
#include "exception_handling.h"
#include "persistence/utilities.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <seastar/core/coroutine.hh>

#include <stdexcept>
#include <utility>
#include <variant>

namespace sea::http::handlers::file_upload {

namespace {

using sea::application::FileService;
using sea::infrastructure::runtime::DynamicRecord;
using sea::infrastructure::runtime::DynamicValue;
using sea::domain::Entity;
using sea::domain::Field;
using sea::domain::FieldType;

// Décode un std::vector<uint8_t> base64 en std::string binaire.
// Réutilise le base64_decode existant de utilities.h.
std::string base64_to_string(const std::string& b64) {
    const auto bytes =
        sea::infrastructure::persistence::utilities::base64_decode(b64);
    return std::string(reinterpret_cast<const char*>(bytes.data()),
                       bytes.size());
}

// Extrait { filename, mime_type, content_base64 } d'un nlohmann::json.
// Lève std::runtime_error si format invalide.
struct JsonFilePayload {
    std::string filename;
    std::string mime_type;
    std::string content;   // déjà décodé
};

JsonFilePayload parse_json_file_object(const nlohmann::json& j) {
    if (!j.is_object()) {
        throw std::runtime_error(
            "Champ file en JSON doit etre un objet "
            "{filename, mime_type, content_base64}.");
    }

    JsonFilePayload p{};

    if (auto it = j.find("filename"); it != j.end() && it->is_string()) {
        p.filename = it->get<std::string>();
    } else {
        throw std::runtime_error("Champ file: 'filename' string requis.");
    }

    if (auto it = j.find("mime_type"); it != j.end() && it->is_string()) {
        p.mime_type = it->get<std::string>();
    } else {
        // Toléré : fallback à octet-stream
        p.mime_type = "application/octet-stream";
    }

    if (auto it = j.find("content_base64"); it != j.end() && it->is_string()) {
        p.content = base64_to_string(it->get<std::string>());
    } else {
        throw std::runtime_error("Champ file: 'content_base64' string requis.");
    }

    return p;
}

} // namespace

// ─────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────
FileUploadExtractor::FileUploadExtractor(
    std::shared_ptr<FileService> file_service)
    : file_service_(std::move(file_service))
{}

// ─────────────────────────────────────────────────────────────
// Helpers privés
// ─────────────────────────────────────────────────────────────
const Field*
FileUploadExtractor::find_file_field(const Entity& entity,
                                     std::string_view name) noexcept
{
    for (const auto& f : entity.fields) {
        if (f.name == name && f.is_file_field()) {
            return &f;
        }
    }
    return nullptr;
}

// ─────────────────────────────────────────────────────────────
// is_multipart_request
// ─────────────────────────────────────────────────────────────
bool FileUploadExtractor::is_multipart_request(
    const seastar::http::request& req) noexcept
{
    // Combine le helper Seastar (classification rapide) avec notre
    // extract_boundary (pour s'assurer qu'on saura parser).
    if (!req.is_multi_part()) {
        return false;
    }
    const auto content_type = req.get_header("Content-Type");
    const auto boundary = sea::http::utils::multipart::extract_boundary(
        std::string_view(content_type.data(), content_type.size()));
    return boundary.has_value();
}

// ─────────────────────────────────────────────────────────────
// extract_from_multipart
// ─────────────────────────────────────────────────────────────
seastar::future<ExtractionResult>
FileUploadExtractor::extract_from_multipart(
    const seastar::http::request& req,
    const std::string& body,
    const Entity& entity,
    DynamicRecord& record)
{
    auto log = spdlog::get("sea.http");
    ExtractionResult result{};

    // 1. Extraire le boundary
    const auto content_type_h = req.get_header("Content-Type");
    auto boundary = sea::http::utils::multipart::extract_boundary(
        std::string_view(content_type_h.data(), content_type_h.size()));
    if (!boundary.has_value()) {
        throw sea_errors_handling::StorageException(
            "[FileUploadExtractor] Content-Type 'multipart/form-data' sans boundary valide.");
    }

    // 2. Parser le body multipart
    sea::http::utils::multipart::ParsedMultipart parsed;
    try {
        parsed = sea::http::utils::multipart::parse(body, *boundary);
    } catch (const std::runtime_error& e) {
        throw sea_errors_handling::StorageException(
            std::string("[FileUploadExtractor] body multipart invalide : ") + e.what());
    }

    // 3. Pour chaque text part : on remplit le record + on l'ajoute
    //    à result.text_parts pour permettre au caller de l'auditer
    for (const auto& tp : parsed.text_parts) {
        // Si l'entité a un champ avec ce nom (et que ce n'est pas un
        // File), on injecte la valeur comme string. Le caller peut
        // re-typer plus tard si nécessaire via JsonRecordParser
        // ou conversion.
        record[tp.name] = tp.value;
        result.text_parts.emplace_back(tp.name, tp.value);
    }

    // 4. Pour chaque file part : on cherche un champ File du même
    //    nom dans l'entité. Si trouvé, on upload via FileService et
    //    on substitue dans le record. Si pas trouvé, on ignore
    //    silencieusement (un client peut envoyer des champs non
    //    déclarés — c'est tolérant).
    for (const auto& fp : parsed.file_parts) {
        const Field* field = find_file_field(entity, fp.name);
        if (field == nullptr) {
            log->debug(
                "FileUploadExtractor: file part '{}' ignored "
                "(no matching File field in entity '{}')",
                fp.name, entity.name);
            continue;
        }

        // Upload — peut throw en cas de validation refusée ou erreur I/O.
        // Si ça throw APRÈS qu'un autre fichier ait été uploadé, les
        // UUIDs déjà dans result.uploaded_uuids serviront au handler
        // pour rollback.
        sea::application::UploadResult upload_result;
        try {
            upload_result = co_await file_service_->upload(
                *field->file_config,
                fp.filename,
                fp.content_type,
                fp.content);   // copie : on garde fp pour la suite si besoin
        } catch (...) {
            // On laisse remonter. Le caller verra l'exception et,
            // grâce aux uuids déjà dans result, pourra rollback().
            // On rejette même result si le caller veut récupérer.
            // ⚠ : ne pas mettre result.had_files=false ici car
            // les UUIDs précédents existent vraiment.
            throw;
        }

        // Substitue dans le record : la colonne SQL stocke l'UUID
        // (un BINARY(16) côté MySQL).
        record[field->name] = upload_result.uuid;

        result.uploaded_uuids.push_back(upload_result.uuid);
        result.had_files = true;

        log->info("FileUploadExtractor: uploaded '{}' as uuid={} "
                  "(field='{}', size={} bytes)",
                  fp.filename, upload_result.uuid, field->name,
                  upload_result.size_bytes);
    }

    co_return result;
}

// ─────────────────────────────────────────────────────────────
// upload_single_part
//
// Wrapper minimal autour de FileService::upload pour un PartFile.
// N'effectue PAS de check is_file_field — c'est au caller de
// l'avoir fait avant (impl: assertion en debug build).
// ─────────────────────────────────────────────────────────────
seastar::future<sea::application::UploadResult>
FileUploadExtractor::upload_single_part(
    const Field& field,
    const sea::http::utils::multipart::PartFile& part)
{
    if (!field.is_file_field()) {
        // Garde-fou : on ne devrait jamais arriver ici sans is_file_field,
        // mais on protège plutôt que de crash sur l'accès au file_config.
        throw sea_errors_handling::StorageException(
            "[FileUploadExtractor] upload_single_part: field '" +
            field.name + "' n'est pas un champ File configure.");
    }

    co_return co_await file_service_->upload(
        *field.file_config,
        part.filename,
        part.content_type,
        part.content);   // copie du contenu binaire
}

// ─────────────────────────────────────────────────────────────
// extract_from_json_record
//
// On itère sur les champs File de l'entité. Pour chaque champ :
//   - récupère sa valeur dans le record
//   - si c'est une string  → suppose UUID déjà existant, skip
//   - si c'est un json     → décode + upload + substitue
//   - sinon                → skip (le record validator s'en
//                             chargera si requis)
// ─────────────────────────────────────────────────────────────
seastar::future<ExtractionResult>
FileUploadExtractor::extract_from_json_record(
    const Entity& entity,
    DynamicRecord& record)
{
    auto log = spdlog::get("sea.http");
    ExtractionResult result{};

    for (const auto& field : entity.fields) {
        if (!field.is_file_field()) {
            continue;
        }

        auto it = record.find(field.name);
        if (it == record.end()) {
            // Champ absent du payload — OK si optionnel, le validator
            // se chargera de rejeter si required.
            continue;
        }

        const DynamicValue& dv = it->second;

        // Cas 1 : la valeur est une string → on considère que c'est
        // un UUID référence à un fichier existant. Pas d'upload.
        if (std::holds_alternative<std::string>(dv)) {
            log->debug("FileUploadExtractor: field '{}' is string "
                       "(assumed UUID reference, no upload)", field.name);
            continue;
        }

        // Cas 2 : la valeur est un nlohmann::json (le JsonRecordParser
        // a parsé l'objet brut). On extrait filename/mime/base64.
        if (std::holds_alternative<nlohmann::json>(dv)) {
            const auto& j = std::get<nlohmann::json>(dv);

            JsonFilePayload payload;
            try {
                payload = parse_json_file_object(j);
            } catch (const std::runtime_error& e) {
                throw sea_errors_handling::StorageException(
                    "[FileUploadExtractor] Champ '" + field.name +
                    "': " + e.what());
            }

            // Upload via FileService — peut throw, propagation au caller.
            auto upload_result = co_await file_service_->upload(
                *field.file_config,
                payload.filename,
                payload.mime_type,
                std::move(payload.content));

            record[field.name] = upload_result.uuid;
            result.uploaded_uuids.push_back(upload_result.uuid);
            result.had_files = true;

            log->info("FileUploadExtractor (JSON): uploaded '{}' as uuid={} "
                      "(field='{}', size={} bytes)",
                      payload.filename, upload_result.uuid, field.name,
                      upload_result.size_bytes);
            continue;
        }

        // Cas 3 : autre type (int, bool, ...) → erreur. Un champ File
        // ne peut être qu'une string (UUID) ou un objet JSON {filename,...}.
        throw sea_errors_handling::StorageException(
            "[FileUploadExtractor] Champ '" + field.name +
            "': type de valeur invalide pour un champ file "
            "(attendu: UUID string ou objet {filename, mime_type, content_base64}).");
    }

    co_return result;
}

// ─────────────────────────────────────────────────────────────
// rollback
//
// Pour chaque UUID uploadé pendant l'extraction : on appelle
// release(uuid, Cascade) qui va décrémenter (de 0 à -1, ou simplement
// observer que reference_count <= 0) puis supprimer le record
// sea_files + le fichier physique.
//
// Note : juste après upload(), reference_count == 0. Le release()
// avec Cascade va :
//   1. décrémenter à -1 (no-op métier mais évite branches spéciales)
//   2. find → ref_count <= 0 → DELETE row + storage
//
// FileService::release fait déjà cette logique correctement.
// ─────────────────────────────────────────────────────────────
seastar::future<>
FileUploadExtractor::rollback(const ExtractionResult& result)
{
    auto log = spdlog::get("sea.http");

    if (result.uploaded_uuids.empty()) {
        co_return;
    }

    log->warn("FileUploadExtractor::rollback : releasing {} uploaded file(s)",
              result.uploaded_uuids.size());

    for (const auto& uuid : result.uploaded_uuids) {
        try {
            co_await file_service_->release(
                uuid, sea::domain::OnDeleteFile::Cascade);
        } catch (const std::exception& e) {
            // Best-effort : si un rollback échoue, on log et on continue
            // avec les autres. Le fichier orphelin sera collecté par
            // release_orphans offline.
            log->error("FileUploadExtractor::rollback failed for uuid={}: {}",
                       uuid, e.what());
        }
    }
    co_return;
}

// ─────────────────────────────────────────────────────────────
// commit
// ─────────────────────────────────────────────────────────────
seastar::future<bool>
FileUploadExtractor::commit(const ExtractionResult& result)
{
    auto log = spdlog::get("sea.http");
    bool all_ok = true;

    for (const auto& uuid : result.uploaded_uuids) {
        const bool ok = co_await file_service_->retain(uuid);
        if (!ok) {
            log->error("FileUploadExtractor::commit: retain failed for uuid={}",
                       uuid);
            all_ok = false;
        }
    }
    co_return all_ok;
}

// ─────────────────────────────────────────────────────────────
// release_old_uuid
// ─────────────────────────────────────────────────────────────
seastar::future<bool>
FileUploadExtractor::release_old_uuid(const std::string& uuid,
                                      sea::domain::OnDeleteFile rule)
{
    auto log = spdlog::get("sea.http");
    try {
        co_return co_await file_service_->release(uuid, rule);
    } catch (const std::exception& e) {
        log->error("FileUploadExtractor::release_old_uuid({}): {}", uuid, e.what());
        co_return false;
    }
}

} // namespace sea::http::handlers::file_upload