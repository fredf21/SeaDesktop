#include "in_memory_generic_repository.h"

#include <variant>
#include <iostream>
#include <algorithm>

namespace sea::infrastructure::persistence {

namespace {

/**
 * Convertit un DynamicValue en string.
 *
 * Utilisé pour :
 * - comparer des champs (find_one_by_field)
 * - générer des clés pivot
 */
std::optional<std::string> dynamic_value_to_string(const runtime::DynamicValue& value)
{
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

    return std::nullopt;
}

/**
 * Génère une clé unique pour une relation pivot (many-to-many).
 *
 * Exemple :
 * { user_id=1, role_id=2 } → "role_id=2;user_id=1;"
 *
 * Important :
 * - tri des clés → ordre stable
 */
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
        auto value_opt = dynamic_value_to_string(values.at(key));

        if (!value_opt.has_value()) {
            continue; // sécurité
        }

        result += key;
        result += "=";
        result += *value_opt;
        result += ";";
    }

    return result;
}

} // namespace

/**
 * Extrait l'ID depuis un record.
 *
 * Supporte :
 * - string
 * - int64
 */
std::optional<std::string>
InMemoryGenericRepository::extract_id(const runtime::DynamicRecord& record) const
{
    const auto it = record.find("id");
    if (it == record.end()) return std::nullopt;

    if (std::holds_alternative<std::string>(it->second))
        return std::get<std::string>(it->second);

    if (std::holds_alternative<std::int64_t>(it->second))
        return std::to_string(std::get<std::int64_t>(it->second));

    return std::nullopt;
}

/**
 * CREATE
 *
 * Ajoute un record en mémoire.
 *
 * Règles :
 * - ID obligatoire
 * - pas d’overwrite silencieux
 * - ignore les champs many-to-many
 */
seastar::future<std::optional<runtime::DynamicRecord>>
InMemoryGenericRepository::create(const std::string& entity_name,
                                  runtime::DynamicRecord record)
{
    const auto id = extract_id(record);

    if (!id.has_value()) {
        return seastar::make_ready_future<std::optional<runtime::DynamicRecord>>(std::nullopt);
    }

    auto& entity_storage = storage_[entity_name];

    // Empêche écrasement silencieux
    if (entity_storage.contains(*id)) {
        return seastar::make_ready_future<std::optional<runtime::DynamicRecord>>(std::nullopt);
    }

    runtime::DynamicRecord filtered_record;

    for (auto& [key, value] : record) {
        // Ignore les relations many-to-many
        if (std::holds_alternative<std::vector<std::string>>(value)) {
            continue;
        }

        filtered_record[key] = std::move(value);
    }

    entity_storage[*id] = filtered_record;

    return seastar::make_ready_future<std::optional<runtime::DynamicRecord>>(filtered_record);
}

/**
 * FIND ALL
 */
seastar::future<std::vector<runtime::DynamicRecord>>
InMemoryGenericRepository::find_all(const std::string& entity_name)
{
    std::vector<runtime::DynamicRecord> result;

    const auto it = storage_.find(entity_name);
    if (it == storage_.end()) {
        return seastar::make_ready_future<std::vector<runtime::DynamicRecord>>(result);
    }

    result.reserve(it->second.size());

    for (const auto& [_, record] : it->second) {
        result.push_back(record);
    }

    return seastar::make_ready_future<std::vector<runtime::DynamicRecord>>(result);
}

/**
 * FIND BY ID
 */
seastar::future<std::optional<runtime::DynamicRecord>>
InMemoryGenericRepository::find_by_id(const std::string& entity_name,
                                      const std::string& id)
{
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

/**
 * FIND ONE BY FIELD
 */
seastar::future<std::optional<runtime::DynamicRecord>>
InMemoryGenericRepository::find_one_by_field(const std::string& entity_name,
                                             const std::string& field_name,
                                             const std::string& value)
{
    const auto entity_it = storage_.find(entity_name);
    if (entity_it == storage_.end()) {
        return seastar::make_ready_future<std::optional<runtime::DynamicRecord>>(std::nullopt);
    }

    for (const auto& [_, record] : entity_it->second) {
        const auto field_it = record.find(field_name);
        if (field_it == record.end()) continue;

        const auto field_value = dynamic_value_to_string(field_it->second);

        if (field_value.has_value() && *field_value == value) {
            return seastar::make_ready_future<std::optional<runtime::DynamicRecord>>(record);
        }
    }

    return seastar::make_ready_future<std::optional<runtime::DynamicRecord>>(std::nullopt);
}

/**
 * DELETE
 */
seastar::future<bool>
InMemoryGenericRepository::remove(const std::string& entity_name,
                                  const std::string& id)
{
    const auto it = storage_.find(entity_name);
    if (it == storage_.end()) {
        return seastar::make_ready_future<bool>(false);
    }

    return seastar::make_ready_future<bool>(it->second.erase(id) > 0);
}

/**
 * UPDATE
 */
seastar::future<UpdateResponse>
InMemoryGenericRepository::update(const std::string& entity_name,
                                  const std::string& id,
                                  runtime::DynamicRecord record)
{
    UpdateResponse response;

    const auto it = storage_.find(entity_name);
    if (it == storage_.end()) {
        return seastar::make_ready_future<UpdateResponse>(response);
    }

    const auto rec_it = it->second.find(id);
    if (rec_it == it->second.end()) {
        return seastar::make_ready_future<UpdateResponse>(response);
    }

    auto& existing = rec_it->second;

    for (auto& [key, value] : record) {
        if (key == "id") continue;

        // Ignore many-to-many
        if (std::holds_alternative<std::vector<std::string>>(value)) {
            continue;
        }

        existing[key] = std::move(value);
    }

    response.record = existing;
    response.status = true;

    return seastar::make_ready_future<UpdateResponse>(response);
}

/**
 * INSERT PIVOT
 *
 * Simule une table pivot en mémoire.
 */
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