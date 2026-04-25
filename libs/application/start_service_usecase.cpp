#include "start_service_usecase.h"

namespace sea::application {

std::shared_ptr<infrastructure::persistence::IGenericRepository>
StartServiceUseCase::execute(
    const sea::domain::Service& service,
    std::shared_ptr<infrastructure::runtime::SchemaRuntimeRegistry> registry,
    const infrastructure::persistence::RepositoryFactory::DatabaseResources& resources
    ) const {

    registry->register_schema(service.schema);

    return repository_factory_.create(
        service.database_config,
        registry,
        resources
        );
}
} // namespace sea::application
