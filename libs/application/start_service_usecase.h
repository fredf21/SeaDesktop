#pragma once

#include "persistence/i_generic_repository.h"
#include "persistence/repository_factory.h"
#include "service.h"
#include "persistence/repository_factory.h"
#include "runtime/schema_runtime_registry.h"
#include "thread_pool_execution/i_blocking_executor.h"
#include "thread_pool_execution/i_blocking_executor.h"

#include <memory>

namespace sea::application {

class StartServiceUseCase {
public:
    std::shared_ptr<sea::infrastructure::persistence::IGenericRepository>
    execute(
        const sea::domain::Service& service,
        std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry> registry,
        const sea::infrastructure::persistence::RepositoryFactory::DatabaseResources& resources,
        std::shared_ptr<IBlockingExecutor> blocking_executor
        ) const;

private:
    sea::infrastructure::persistence::RepositoryFactory repository_factory_;
};

} // namespace sea::application