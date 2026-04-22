#include "repository_factory.h"

#include "memory/in_memory_generic_repository.h"
#include "mysql/mysql_generic_repository.h"
#include <stdexcept>

namespace sea::infrastructure::persistence {

std::unique_ptr<IGenericRepository>
RepositoryFactory::create(const sea::domain::DatabaseConfig& config, const runtime::SchemaRuntimeRegistry &schema_registry) const {
    switch (config.type) {
    case sea::domain::DatabaseType::Memory:
        return std::make_unique<InMemoryGenericRepository>();

    case sea::domain::DatabaseType::PostgreSQL:
        throw std::runtime_error(
            "PostgreSQL n'est pas encore implemente dans le MVP."
            );

    case sea::domain::DatabaseType::MongoDB:
        throw std::runtime_error(
            "MongoDB n'est pas encore implemente dans le MVP."
            );
    case domain::DatabaseType::MySQL:
        auto connector = std::make_unique<mysql::MySQLConnector>(
            config.host,
            config.username,
            config.password,
            config.database_name,
            static_cast<unsigned int>(config.port)
            );
        return std::make_unique<mysql::MySQLGenericRepository>(
            std::move(connector),
            schema_registry
            );
        break;
    }

    throw std::runtime_error("Type de base de donnees inconnu.");
}

} // namespace sea::infrastructure::persistence
