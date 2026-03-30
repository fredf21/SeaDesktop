#pragma once

#include "i_generic_repository.h"
#include "database_config.h"

#include <memory>

namespace sea::infrastructure::persistence {

// ─────────────────────────────────────────────────────────────
// RepositoryFactory
//
// Choisit l’implémentation de repository à utiliser en fonction
// du DatabaseConfig du Service.
// ─────────────────────────────────────────────────────────────
class RepositoryFactory {
public:
    [[nodiscard]] std::unique_ptr<IGenericRepository>
    create(const sea::domain::DatabaseConfig& config) const;
};

} // namespace sea::infrastructure::persistence
