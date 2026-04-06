#pragma once

#include "generic_validator.h"
#include "schema_runtime_registry.h"
#include "persistence/i_generic_repository.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sea::infrastructure::runtime {

// ─────────────────────────────────────────────────────────────
// GenericCrudEngine
//
// Moteur CRUD générique du MVP.
// Il utilise :
// - un registry de schéma
// - un validateur runtime
// - un repository générique
//
// Important : cette version stocke des shared_ptr,
// ce qui évite les références pendantes dans le serveur HTTP.
// ─────────────────────────────────────────────────────────────
class GenericCrudEngine {
public:
    struct OperationResult {
        bool success{false};
        std::optional<DynamicRecord> record;
        std::vector<std::string> errors;
    };

    GenericCrudEngine(
        std::shared_ptr<SchemaRuntimeRegistry> registry,
        std::shared_ptr<GenericValidator> validator,
        std::shared_ptr<sea::infrastructure::persistence::IGenericRepository> repository)
        : registry_(std::move(registry)),
        validator_(std::move(validator)),
        repository_(std::move(repository)) {
    }

    [[nodiscard]] OperationResult create(const std::string& entity_name,
                                         DynamicRecord record) const;

    [[nodiscard]] std::vector<DynamicRecord>
    list(const std::string& entity_name) const;

    [[nodiscard]] std::optional<DynamicRecord>
    get_by_id(const std::string& entity_name,
              const std::string& id) const;

    [[nodiscard]] OperationResult update(const std::string& entity_name,
                                         const std::string& id,
                                         DynamicRecord record) const;

    [[nodiscard]] bool remove(const std::string& entity_name,
                              const std::string& id) const;

private:
    std::shared_ptr<SchemaRuntimeRegistry> registry_;
    std::shared_ptr<GenericValidator> validator_;
    std::shared_ptr<sea::infrastructure::persistence::IGenericRepository> repository_;
};

} // namespace sea::infrastructure::runtime