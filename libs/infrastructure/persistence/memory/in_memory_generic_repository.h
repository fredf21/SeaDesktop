#pragma once

#include "../i_generic_repository.h"

#include <unordered_map>

namespace sea::infrastructure::persistence {

// ─────────────────────────────────────────────────────────────
// InMemoryGenericRepository
//
// Implémentation MVP de la persistence.
// Les données vivent uniquement en mémoire RAM.
// ─────────────────────────────────────────────────────────────
class InMemoryGenericRepository final : public IGenericRepository {
public:
    void create(const std::string& entity_name,
                runtime::DynamicRecord record) override;

    [[nodiscard]] std::vector<runtime::DynamicRecord>
    find_all(const std::string& entity_name) const override;

    [[nodiscard]] std::optional<runtime::DynamicRecord>
    find_by_id(const std::string& entity_name,
               const std::string& id) const override;

    bool remove(const std::string& entity_name,
                const std::string& id) override;

    bool update(const std::string& entity_name,
                const std::string& id,
                runtime::DynamicRecord record) override;

private:
    using EntityStorage = std::unordered_map<std::string, runtime::DynamicRecord>;

    // entity_name -> (id -> record)
    std::unordered_map<std::string, EntityStorage> storage_;

    [[nodiscard]] std::optional<std::string>
    extract_id(const runtime::DynamicRecord& record) const;
};

} // namespace sea::infrastructure::persistence