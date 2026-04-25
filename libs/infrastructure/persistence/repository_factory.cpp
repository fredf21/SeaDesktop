#include "repository_factory.h"

#include "memory/in_memory_generic_repository.h"
#include "mysql/mysql_generic_repository.h"

#include <stdexcept>

namespace sea::infrastructure::persistence {
std::shared_ptr<IGenericRepository>
RepositoryFactory::create(const sea::domain::DatabaseConfig& config,
                          std::shared_ptr<runtime::SchemaRuntimeRegistry> schema_registry,
                          const DatabaseResources& resources) const
{
    if (!schema_registry) {
        throw std::runtime_error("RepositoryFactory: schema_registry manquant.");
    }

    switch (config.type) {
    case sea::domain::DatabaseType::Memory:
        return std::make_shared<InMemoryGenericRepository>();

    case sea::domain::DatabaseType::PostgreSQL:
        throw std::runtime_error("PostgreSQL n'est pas encore implemente dans le MVP.");

    case sea::domain::DatabaseType::MongoDB:
        throw std::runtime_error("MongoDB n'est pas encore implemente dans le MVP.");

    case sea::domain::DatabaseType::MySQL:
        if (!resources.mysql_pool) {
            throw std::runtime_error(
                "RepositoryFactory: mysql_pool manquant pour une configuration MySQL."
                );
        }

        return std::make_shared<mysql::MySQLGenericRepository>(
            *resources.mysql_pool,
            std::move(schema_registry)
            );
    }

    throw std::runtime_error("Type de base de donnees inconnu.");
}

} // namespace sea::infrastructure::persistence