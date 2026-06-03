#include "fileservice.h"
#include "exception_handling.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>
#include <utility>

#include <spdlog/spdlog.h>
#include <seastar/core/coroutine.hh>

namespace sea::application {

namespace {

// Extrait l'extension d'un nom de fichier (incluant le point).
// Retourne vide si pas d'extension trouvee. Lowercase.
std::string extract_extension(const std::string& original_name) {
    const auto pos = original_name.rfind('.');
    if (pos == std::string::npos || pos == original_name.size() - 1) {
        return "";
    }
    std::string ext = original_name.substr(pos);  // inclut le '.'
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return ext;
}

} // namespace

// ─────────────────────────────────────────────────────────────
// generate_uuid (statique, prive)
//
// Implementation UUID v4 minimaliste, autonome. Pas de dependance
// vers http_utils ou utility externe : FileService est self-sufficient.
// ─────────────────────────────────────────────────────────────
std::string FileService::generate_uuid()
{
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<std::uint64_t> dis;

    std::uint64_t part1 = dis(gen);
    std::uint64_t part2 = dis(gen);

    // Force version v4 + variant DCE 1.1
    part1 = (part1 & 0xffffffffffff0fffULL) | 0x0000000000004000ULL;
    part2 = (part2 & 0x3fffffffffffffffULL) | 0x8000000000000000ULL;

    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(8) << (part1 >> 32) << "-"
        << std::setw(4) << ((part1 >> 16) & 0xffff) << "-"
        << std::setw(4) << (part1 & 0xffff) << "-"
        << std::setw(4) << (part2 >> 48) << "-"
        << std::setw(12) << (part2 & 0xffffffffffffULL);

    return oss.str();
}

// ─────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────
FileService::FileService(
    std::shared_ptr<sea::infrastructure::persistence::FileRepository> repo,
    std::shared_ptr<sea::infrastructure::storage::IFileStorage> storage,
    std::shared_ptr<IBlockingExecutor> blocking_executor)
    : repo_(std::move(repo))
    , storage_(std::move(storage))
    , blocking_executor_(std::move(blocking_executor))
{}

// ─────────────────────────────────────────────────────────────
// validate_upload (statique)
// ─────────────────────────────────────────────────────────────
UploadValidationResult
FileService::validate_upload(const sea::domain::FileFieldConfig& config,
                             const std::string& original_name,
                             const std::string& mime_type,
                             std::size_t content_size)
{
    UploadValidationResult vr{};

    if (!config.accepts_size(content_size)) {
        vr.accepted = false;
        vr.error_message = "Fichier trop volumineux (" +
                           std::to_string(content_size) +
                           " bytes ; max " +
                           std::to_string(config.max_size_bytes.value_or(0)) +
                           ").";
        return vr;
    }

    if (!config.accepts_mime(mime_type)) {
        vr.accepted = false;
        vr.error_message = "Type MIME refuse: '" + mime_type + "'.";
        return vr;
    }

    const std::string ext = extract_extension(original_name);
    if (!config.accepts_extension(ext)) {
        vr.accepted = false;
        vr.error_message = "Extension refusee: '" +
                           (ext.empty() ? "(aucune)" : ext) + "'.";
        return vr;
    }

    return vr;
}

// ─────────────────────────────────────────────────────────────
// build_target_path (statique)
// ─────────────────────────────────────────────────────────────
std::string
FileService::build_target_path(const std::string& storage_path,
                               const std::string& uuid,
                               const std::string& original_name)
{
    const std::string ext = extract_extension(original_name);

    // Concatenation : storage_path/uuid[.ext]
    std::string path = storage_path;
    if (!path.empty() && path.back() != '/') {
        path.push_back('/');
    }
    path += uuid;
    path += ext;   // peut etre vide

    return path;
}

// ─────────────────────────────────────────────────────────────
// upload
// ─────────────────────────────────────────────────────────────
seastar::future<UploadResult>
FileService::upload(const sea::domain::FileFieldConfig& config,
                    const std::string& original_name,
                    const std::string& mime_type,
                    std::string content)
{
    auto log = spdlog::get("sea.application");

    // 1. Validation
    const auto vr = validate_upload(config, original_name, mime_type, content.size());
    if (!vr.accepted) {
        log->warn("File upload rejected: {}", vr.error_message);
        throw sea_errors_handling::StorageException(
            "[FileService] " + vr.error_message);
    }

    // 2. UUID + path cible
    const std::string uuid = generate_uuid();
    const std::string target_path = build_target_path(
        config.storage_path, uuid, original_name);

    // 3. Ecriture disque (bloquante -> blocking executor)
    auto storage = storage_;
    co_await blocking_executor_->submit([storage, target_path, &content]() {
        storage->store(target_path, content);
    });

    log->info("File written to storage: {} ({} bytes)",
              target_path, content.size());

    // 4. INSERT sea_files
    sea::domain::FileMetadata meta{};
    meta.id              = uuid;
    meta.original_name   = original_name;
    meta.mime_type       = mime_type;
    meta.size_bytes      = content.size();
    meta.storage_path    = target_path;
    meta.reference_count = 0;
    meta.created_at      = std::chrono::system_clock::now();

    const bool inserted = co_await repo_->insert(meta);
    if (!inserted) {
        log->error("INSERT sea_files failed for uuid={}, rolling back storage",
                   uuid);
        try {
            co_await blocking_executor_->submit(
                [storage, target_path]() { storage->remove(target_path); });
        } catch (const std::exception& e) {
            log->error("Failed to rollback storage for uuid={}: {}",
                       uuid, e.what());
        }
        throw sea_errors_handling::StorageException(
            "[FileService] echec INSERT sea_files pour uuid=" + uuid);
    }

    UploadResult result{};
    result.uuid         = uuid;
    result.storage_path = target_path;
    result.size_bytes   = meta.size_bytes;
    result.mime_type    = mime_type;
    co_return result;
}

// ─────────────────────────────────────────────────────────────
// download
// ─────────────────────────────────────────────────────────────
seastar::future<std::optional<FileService::DownloadResult>>
FileService::download(const std::string& uuid)
{
    auto meta_opt = co_await repo_->find_by_id(uuid);
    if (!meta_opt.has_value()) {
        co_return std::nullopt;
    }

    auto storage = storage_;
    const std::string path = meta_opt->storage_path;

    std::string content = co_await blocking_executor_->submit(
        [storage, path]() -> std::string {
            return storage->retrieve(path);
        });

    DownloadResult result{};
    result.metadata = std::move(*meta_opt);
    result.content  = std::move(content);
    co_return result;
}

// ─────────────────────────────────────────────────────────────
// retain
// ─────────────────────────────────────────────────────────────
seastar::future<bool>
FileService::retain(const std::string& uuid)
{
    const bool ok = co_await repo_->add_reference(uuid);
    if (!ok) {
        spdlog::get("sea.application")->warn(
            "FileService::retain failed for uuid={}", uuid);
    }
    co_return ok;
}

// ─────────────────────────────────────────────────────────────
// release
// ─────────────────────────────────────────────────────────────
seastar::future<bool>
FileService::release(const std::string& uuid,
                     sea::domain::OnDeleteFile rule)
{
    auto log = spdlog::get("sea.application");

    if (rule == sea::domain::OnDeleteFile::Restrict) {
        log->warn(
            "FileService::release called with Restrict for uuid={} -- "
            "fail-safe: treating as SetNull (preserve file)",
            uuid);
        rule = sea::domain::OnDeleteFile::SetNull;
    }

    // Décrément ATOMIQUE et conditionnel : ne descend jamais sous
    // zéro. Si false, le compteur était déjà <= 0 (release sans
    // retain correspondant, ou uuid inconnu) -- on refuse sans
    // toucher au fichier.
    const bool dec_ok = co_await repo_->release_reference_if_positive(uuid);
    if (!dec_ok) {
        log->warn(
            "FileService::release: refused for uuid={} -- reference_count "
            "already <= 0 or unknown uuid (release without matching retain)",
            uuid);
        co_return false;
    }

    if (rule == sea::domain::OnDeleteFile::SetNull) {
        co_return true;
    }

    // Cascade : on relit le compteur. S'il est a 0, on supprime.
    auto meta_opt = co_await repo_->find_by_id(uuid);
    if (!meta_opt.has_value()) {
        co_return true;
    }

    if (meta_opt->reference_count > 0) {
        co_return true;
    }

    const std::string path = meta_opt->storage_path;

    const bool row_deleted = co_await repo_->delete_row(uuid);
    if (!row_deleted) {
        log->warn(
            "FileService::release: delete_row failed for uuid={} "
            "(probably still referenced by RESTRICT FK) -- file kept",
            uuid);
        co_return true;
    }

    auto storage = storage_;
    try {
        co_await blocking_executor_->submit(
            [storage, path]() { storage->remove(path); });
        log->info("File physically removed from storage: {}", path);
    } catch (const std::exception& e) {
        log->error("File deleted from sea_files but storage remove failed "
                   "for path={}: {}", path, e.what());
    }

    co_return true;
}
} // namespace sea::application