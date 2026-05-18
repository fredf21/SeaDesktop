#include "filerepository.h"
#include "persistence/mysql/sea_files_table.h"

#include <chrono>
#include <utility>
#include <variant>
#include <seastar/core/coroutine.hh>
#include <spdlog/spdlog.h>

namespace sea::infrastructure::persistence {

namespace {

using mysql::SeaFilesTable;
using runtime::DynamicRecord;
using runtime::DynamicValue;

// Helper : extrait une string d'un DynamicValue, ou nullopt si pas une string.
std::optional<std::string> get_string(const DynamicValue& v) {
    if (std::holds_alternative<std::string>(v)) {
        return std::get<std::string>(v);
    }
    return std::nullopt;
}

// Helper : extrait un int (en castant le numerique) d'un DynamicValue.
// Renvoie 0 si type incompatible (un compteur manquant = 0 logique).
std::int64_t get_int64(const DynamicValue& v) {
    return std::visit(
        [](auto&& arg) -> std::int64_t {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
                return static_cast<std::int64_t>(arg);
            }
            return 0;
        },
        v);
}

// Helper : timestamp string MySQL -> time_point.
// Format attendu : "YYYY-MM-DD HH:MM:SS" (MySQL DEFAULT).
// En cas de parsing impossible, retourne epoch — ce n'est pas critique
// (le caller affichera epoch comme "1970-01-01" mais le service continue).
std::chrono::system_clock::time_point parse_mysql_timestamp(const std::string& s) {
    std::tm tm{};
    int Y, M, D, h, m, sec;
    if (std::sscanf(s.c_str(), "%d-%d-%d %d:%d:%d",
                    &Y, &M, &D, &h, &m, &sec) != 6) {
        return {};
    }
    tm.tm_year = Y - 1900;
    tm.tm_mon  = M - 1;
    tm.tm_mday = D;
    tm.tm_hour = h;
    tm.tm_min  = m;
    tm.tm_sec  = sec;
    tm.tm_isdst = 0;
    const std::time_t tt = ::timegm(&tm);   // UTC parsing (MySQL TIMESTAMP est en UTC stocke)
    return std::chrono::system_clock::from_time_t(tt);
}

} // namespace

// ─────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────
FileRepository::FileRepository(
    std::shared_ptr<IGenericRepository> repo)
    : repo_(std::move(repo))
{}

// ─────────────────────────────────────────────────────────────
// Conversion FileMetadata -> DynamicRecord
// ─────────────────────────────────────────────────────────────
DynamicRecord
FileRepository::to_record(const sea::domain::FileMetadata& metadata)
{
    DynamicRecord record;

    record[std::string(SeaFilesTable::COL_ID)]              = metadata.id;
    record[std::string(SeaFilesTable::COL_ORIGINAL_NAME)]   = metadata.original_name;
    record[std::string(SeaFilesTable::COL_MIME_TYPE)]       = metadata.mime_type;
    record[std::string(SeaFilesTable::COL_SIZE_BYTES)]      =
        static_cast<std::int64_t>(metadata.size_bytes);
    record[std::string(SeaFilesTable::COL_STORAGE_PATH)]    = metadata.storage_path;
    record[std::string(SeaFilesTable::COL_REFERENCE_COUNT)] =
        static_cast<std::int64_t>(metadata.reference_count);

    // created_at est laisse au DEFAULT CURRENT_TIMESTAMP de la DDL.
    return record;
}

// ─────────────────────────────────────────────────────────────
// Conversion DynamicRecord -> FileMetadata
// ─────────────────────────────────────────────────────────────
std::optional<sea::domain::FileMetadata>
FileRepository::from_record(const DynamicRecord& record)
{
    auto id_it = record.find(std::string(SeaFilesTable::COL_ID));
    if (id_it == record.end()) {
        return std::nullopt;
    }

    sea::domain::FileMetadata meta;
    meta.id = get_string(id_it->second).value_or("");

    if (auto it = record.find(std::string(SeaFilesTable::COL_ORIGINAL_NAME));
        it != record.end()) {
        meta.original_name = get_string(it->second).value_or("");
    }
    if (auto it = record.find(std::string(SeaFilesTable::COL_MIME_TYPE));
        it != record.end()) {
        meta.mime_type = get_string(it->second).value_or("");
    }
    if (auto it = record.find(std::string(SeaFilesTable::COL_SIZE_BYTES));
        it != record.end()) {
        meta.size_bytes = static_cast<std::size_t>(get_int64(it->second));
    }
    if (auto it = record.find(std::string(SeaFilesTable::COL_STORAGE_PATH));
        it != record.end()) {
        meta.storage_path = get_string(it->second).value_or("");
    }
    if (auto it = record.find(std::string(SeaFilesTable::COL_REFERENCE_COUNT));
        it != record.end()) {
        meta.reference_count = static_cast<std::int32_t>(get_int64(it->second));
    }
    if (auto it = record.find(std::string(SeaFilesTable::COL_CREATED_AT));
        it != record.end()) {
        if (auto s = get_string(it->second); s.has_value()) {
            meta.created_at = parse_mysql_timestamp(*s);
        }
    }

    return meta;
}

// ─────────────────────────────────────────────────────────────
// CRUD
// ─────────────────────────────────────────────────────────────
seastar::future<bool>
FileRepository::insert(const sea::domain::FileMetadata& metadata)
{
    auto record = to_record(metadata);
    auto created = co_await repo_->create(
        std::string(SeaFilesTable::TABLE_NAME),
        std::move(record));

    if (!created.has_value()) {
        spdlog::get("sea.persistence")->error(
            "FileRepository::insert failed for uuid={} path={}",
            metadata.id, metadata.storage_path);
        co_return false;
    }
    co_return true;
}

seastar::future<std::optional<sea::domain::FileMetadata>>
FileRepository::find_by_id(const std::string& uuid)
{
    auto record = co_await repo_->find_by_id(
        std::string(SeaFilesTable::TABLE_NAME), uuid);

    if (!record.has_value()) {
        co_return std::nullopt;
    }
    co_return from_record(*record);
}

seastar::future<bool>
FileRepository::add_reference(const std::string& uuid)
{
    co_return co_await repo_->increment_field(
        std::string(SeaFilesTable::TABLE_NAME),
        uuid,
        std::string(SeaFilesTable::COL_REFERENCE_COUNT),
        +1);
}

seastar::future<bool>
FileRepository::release_reference(const std::string& uuid)
{
    co_return co_await repo_->increment_field(
        std::string(SeaFilesTable::TABLE_NAME),
        uuid,
        std::string(SeaFilesTable::COL_REFERENCE_COUNT),
        -1);
}

seastar::future<bool>
FileRepository::delete_row(const std::string& uuid)
{
    co_return co_await repo_->remove(
        std::string(SeaFilesTable::TABLE_NAME), uuid);
}

} // namespace sea::infrastructure::persistence