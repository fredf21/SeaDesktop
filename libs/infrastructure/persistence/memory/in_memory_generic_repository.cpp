#include "in_memory_generic_repository.h"

#include <variant>
#include <iostream>


namespace sea::infrastructure::persistence {
namespace {

std::string dynamic_value_to_string(const runtime::DynamicValue& value)
{
    if (std::holds_alternative<std::monostate>(value)) {
        return "null";
    }
    if (std::holds_alternative<std::string>(value)) {
        return std::get<std::string>(value);
    }
    if (std::holds_alternative<std::int64_t>(value)) {
        return std::to_string(std::get<std::int64_t>(value));
    }
    if (std::holds_alternative<double>(value)) {
        return std::to_string(std::get<double>(value));
    }
    if (std::holds_alternative<bool>(value)) {
        return std::get<bool>(value) ? "true" : "false";
    }

    return "unsupported";
}

std::string make_record_key(const runtime::DynamicRecord& values)
{
    std::vector<std::string> keys;
    keys.reserve(values.size());

    for (const auto& [key, _] : values) {
        keys.push_back(key);
    }

    std::sort(keys.begin(), keys.end());

    std::string result;
    for (const auto& key : keys) {
        result += key;
        result += "=";
        result += dynamic_value_to_string(values.at(key));
        result += ";";
    }

    return result;
}

} // namespace


std::optional<std::string>
InMemoryGenericRepository::extract_id(const runtime::DynamicRecord& record) const {
    const auto it = record.find("id");
    if (it == record.end()) return std::nullopt;

    if (std::holds_alternative<std::string>(it->second))
        return std::get<std::string>(it->second);

    if (std::holds_alternative<std::int64_t>(it->second))
        return std::to_string(std::get<std::int64_t>(it->second));

    return std::nullopt;
}

seastar::future<std::optional<runtime::DynamicRecord>>
InMemoryGenericRepository::create(const std::string& entity_name,
                                  runtime::DynamicRecord record)
{
    const auto id = extract_id(record);

    std::cerr << "[CREATE REPO] entity=" << entity_name
              << " id=" << (id.has_value() ? *id : "NULLOPT") << std::endl;

    if (!id.has_value()) {
        return seastar::make_ready_future<std::optional<runtime::DynamicRecord>>(std::nullopt);
    }

    auto& entity_storage = storage_[entity_name];

    // Eviter overwrite silencieux
    if (entity_storage.contains(*id)) {
        std::cerr << "[CREATE REPO] ID deja existant: " << *id << std::endl;
        return seastar::make_ready_future<std::optional<runtime::DynamicRecord>>(std::nullopt);
    }

    // Filtrer les champs relationnels (important)
    runtime::DynamicRecord filtered_record;

    for (auto& [key, value] : record) {
        // on ignore les tableaux (many-to-many)
        if (std::holds_alternative<std::vector<std::string>>(value)) {
            continue;
        }

        filtered_record[key] = std::move(value);
    }

    entity_storage[*id] = filtered_record;

    return seastar::make_ready_future<std::optional<runtime::DynamicRecord>>(filtered_record);
}

seastar::future<std::vector<runtime::DynamicRecord>>
InMemoryGenericRepository::find_all(const std::string& entity_name)  {
    std::vector<runtime::DynamicRecord> result;

    const auto it = storage_.find(entity_name);
    if (it == storage_.end()) {
        return seastar::make_ready_future<std::vector<runtime::DynamicRecord>>(result);
    }

    result.reserve(it->second.size());

    for (const auto& [id, record] : it->second) {
        result.push_back(record);
    }

    return seastar::make_ready_future<std::vector<runtime::DynamicRecord>>(result);
}

seastar::future<std::optional<runtime::DynamicRecord>>
InMemoryGenericRepository::find_by_id(const std::string& entity_name,
                                      const std::string& id) {
    const auto it = storage_.find(entity_name);
    if (it == storage_.end()) {
        return seastar::make_ready_future<std::optional<runtime::DynamicRecord>>(std::nullopt);
    }

    const auto rec_it = it->second.find(id);
    if (rec_it == it->second.end()) {
        return seastar::make_ready_future<std::optional<runtime::DynamicRecord>>(std::nullopt);
    }

    return seastar::make_ready_future<std::optional<runtime::DynamicRecord>>(rec_it->second);
}

seastar::future<bool> InMemoryGenericRepository::remove(const std::string& entity_name,
                                       const std::string& id) {
    const auto it = storage_.find(entity_name);
    if (it == storage_.end()) {
        return seastar::make_ready_future<bool>(false);
    }

    return seastar::make_ready_future<bool>(it->second.erase(id) > 0);
}

seastar::future<UpdateResponse>
InMemoryGenericRepository::update(const std::string& entity_name,
                                  const std::string& id,
                                  runtime::DynamicRecord record)
{
    UpdateResponse updateResponse;

    const auto it = storage_.find(entity_name);
    if (it == storage_.end()) {
        return seastar::make_ready_future<UpdateResponse>(updateResponse);
    }

    const auto rec_it = it->second.find(id);
    if (rec_it == it->second.end()) {
        return seastar::make_ready_future<UpdateResponse>(updateResponse);
    }

    auto& existing = rec_it->second;

    for (auto& [key, value] : record) {
        if (key == "id") {
            continue;
        }

        // ignorer les champs many-to-many dans la table principale
        if (std::holds_alternative<std::vector<std::string>>(value)) {
            continue;
        }

        existing[key] = std::move(value);
    }

    updateResponse.record = existing;
    updateResponse.status = true;

    return seastar::make_ready_future<UpdateResponse>(updateResponse);
}

seastar::future<bool>
InMemoryGenericRepository::insert_pivot(const std::string& pivot_table,
                                        runtime::DynamicRecord values)
{
    auto& pivot_storage = storage_[pivot_table];
    const std::string synthetic_id = make_record_key(values);

    pivot_storage[synthetic_id] = std::move(values);
    return seastar::make_ready_future<bool>(true);
}

} // namespace sea::infrastructure::persistence