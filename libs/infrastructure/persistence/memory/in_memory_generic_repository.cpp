#include "in_memory_generic_repository.h"

#include <variant>
#include <iostream>

namespace sea::infrastructure::persistence {

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

std::optional<runtime::DynamicRecord> InMemoryGenericRepository::create(const std::string& entity_name,
                                       runtime::DynamicRecord record) {
    const auto id = extract_id(record);
    std::cerr << "[CREATE REPO] entity=" << entity_name
              << " id=" << (id.has_value() ? *id : "NULLOPT") << std::endl;
    if (!id.has_value()) {
        return std::nullopt;
    }

    return storage_[entity_name][*id] = std::move(record);
}

std::vector<runtime::DynamicRecord>
InMemoryGenericRepository::find_all(const std::string& entity_name) const {
    std::vector<runtime::DynamicRecord> result;

    const auto it = storage_.find(entity_name);
    if (it == storage_.end()) {
        return result;
    }

    result.reserve(it->second.size());

    for (const auto& [id, record] : it->second) {
        result.push_back(record);
    }

    return result;
}

std::optional<runtime::DynamicRecord>
InMemoryGenericRepository::find_by_id(const std::string& entity_name,
                                      const std::string& id) const {
    const auto it = storage_.find(entity_name);
    if (it == storage_.end()) {
        return std::nullopt;
    }

    const auto rec_it = it->second.find(id);
    if (rec_it == it->second.end()) {
        return std::nullopt;
    }

    return rec_it->second;
}

bool InMemoryGenericRepository::remove(const std::string& entity_name,
                                       const std::string& id) {
    const auto it = storage_.find(entity_name);
    if (it == storage_.end()) {
        return false;
    }

    return it->second.erase(id) > 0;
}

UpdateResponse InMemoryGenericRepository::update(const std::string& entity_name,
                                       const std::string& id,
                                       runtime::DynamicRecord record) {
    UpdateResponse updateResponse;
    const auto it = storage_.find(entity_name);
    if (it == storage_.end()) {
        return updateResponse;
    }
    const auto rec_it = it->second.find(id);
    if (rec_it == it->second.end()) {
        return updateResponse;
    }
    // Merge — mettre à jour seulement les champs fournis
    auto& existing = rec_it->second;
    for (auto& [key, value] : record) {
        if (key == "id") continue; // ne jamais modifier l'id
        existing[key] = std::move(value);
    }
    // On force l'id dans le record mis à jour
    record["id"] = id;
    updateResponse.record = existing;
    updateResponse.status = true;
    return updateResponse;
}

} // namespace sea::infrastructure::persistence