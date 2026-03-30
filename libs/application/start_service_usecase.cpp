#include "start_service_usecase.h"

namespace sea::application {

std::unique_ptr<sea::infrastructure::persistence::IGenericRepository>
StartServiceUseCase::execute(const sea::domain::Service& service) const {
    registry_.register_schema(service.schema);
    return repository_factory_.create(service.database_config);
}

} // namespace sea::application
