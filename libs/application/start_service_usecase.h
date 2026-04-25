#pragma once

#include <memory>

#include "service.h"
#include "runtime/schema_runtime_registry.h"
#include "persistence/i_generic_repository.h"
#include "persistence/repository_factory.h"

namespace sea::application {

// ─────────────────────────────────────────────────────────────
// StartServiceUseCase
//
// Prépare le runtime pour un service :
// - enregistre le schéma dans le registry
// - instancie le repository adapté au DatabaseConfig
//
// Le lancement réel du serveur Seastar sera fait dans l'app backend.
// ─────────────────────────────────────────────────────────────
class StartServiceUseCase {
public:
    StartServiceUseCase(
        sea::infrastructure::runtime::SchemaRuntimeRegistry& registry,
        sea::infrastructure::persistence::RepositoryFactory& repository_factory)
        : registry_(registry),
        repository_factory_(repository_factory) {
    }

    [[nodiscard]] std::shared_ptr<infrastructure::persistence::IGenericRepository>
    execute(
        const sea::domain::Service& service,
        std::shared_ptr<infrastructure::runtime::SchemaRuntimeRegistry> registry,
        const infrastructure::persistence::RepositoryFactory::DatabaseResources& resources
        ) const;

private:
    sea::infrastructure::runtime::SchemaRuntimeRegistry& registry_;
    sea::infrastructure::persistence::RepositoryFactory& repository_factory_;
};

} // namespace sea::application