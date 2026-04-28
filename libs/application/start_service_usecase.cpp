#include "start_service_usecase.h"

#include <utility>

namespace sea::application {

std::shared_ptr<sea::infrastructure::persistence::IGenericRepository>
StartServiceUseCase::execute(
    const sea::domain::Service& service,
    std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry> registry,
    const sea::infrastructure::persistence::RepositoryFactory::DatabaseResources& resources,
    std::shared_ptr<IBlockingExecutor> blocking_executor
    ) const
{
    /**
     * Enregistre le schéma du service dans le registry runtime.
     *
     * Le repository dynamique utilise ce registry pour connaître :
     * - les entités
     * - les champs
     * - les types
     * - les noms de tables
     */
    registry->register_schema(service.schema);

    /**
     * Crée le repository adapté au backend choisi.
     *
     * Pour MySQL, le blocking_executor est nécessaire parce que
     * MySQL Connector/C++ exécute des appels bloquants.
     */
    return repository_factory_.create(
        service.database_config,
        std::move(registry),
        resources,
        std::move(blocking_executor)
        );
}

} // namespace sea::application