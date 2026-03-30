#include "in_memory_generic_repository.h"

#include <variant>

namespace sea::infrastructure::persistence {

std::optional<std::string>
InMemoryGenericRepository::extract_id(const runtime::DynamicRecord& record) const {
    const auto it = record.find("id");
    if (it == record.end()) {
        return std::nullopt;
    }

    if (!std::holds_alternative<std::string>(it->second)) {
        return std::nullopt;
    }

    return std::get<std::string>(it->second);
}

void InMemoryGenericRepository::create(const std::string& entity_name,
                                       runtime::DynamicRecord record) {
    const auto id = extract_id(record);
    if (!id.has_value()) {
        return;
    }

    storage_[entity_name][*id] = std::move(record);
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

bool InMemoryGenericRepository::update(const std::string& entity_name,
                                       const std::string& id,
                                       runtime::DynamicRecord record) {
    const auto it = storage_.find(entity_name);
    if (it == storage_.end()) {
        return false;
    }

    const auto rec_it = it->second.find(id);
    if (rec_it == it->second.end()) {
        return false;
    }

    // On force l'id dans le record mis à jour
    record["id"] = id;
    rec_it->second = std::move(record);
    return true;
}

} // namespace sea::infrastructure::persistence