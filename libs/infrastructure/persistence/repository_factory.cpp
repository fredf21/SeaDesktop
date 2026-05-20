#include "repository_factory.h"

#include "exception_handling.h"
#include "memory/in_memory_generic_repository.h"
#include "mysql/mysql_generic_repository.h"

#include <stdexcept>
#include <utility>

namespace sea::infrastructure::persistence {

std::shared_ptr<IGenericRepository>
RepositoryFactory::create(
    const sea::domain::DatabaseConfig& config,
    std::shared_ptr<runtime::SchemaRuntimeRegistry> schema_registry,
    const DatabaseResources& resources,
    std::shared_ptr<IBlockingExecutor> blocking_executor) const
{
    try{
        /**
         * Le registry est obligatoire pour les repositories dynamiques.
         * Il permet de connaître les entités, champs, types, tables, etc.
         */
        if (!schema_registry) {
            throw std::runtime_error("RepositoryFactory: missing schema_registry.");
        }

        switch (config.type) {
        case sea::domain::DatabaseType::Memory:
            /**
             * Le repository mémoire ne fait pas d'I/O bloquante.
             * Il n'a donc pas besoin du blocking_executor.
             */
            return std::make_shared<InMemoryGenericRepository>();

        case sea::domain::DatabaseType::PostgreSQL:
            throw std::runtime_error("PostgreSQL is not implemented yet in the MVP.");

        case sea::domain::DatabaseType::MongoDB:
            throw std::runtime_error("MongoDB is not implemented yet in the MVP.");

        case sea::domain::DatabaseType::MySQL:
            /**
             * MySQL Connector/C++ est bloquant.
             *
             * On exige donc :
             * - un pool MySQL déjà démarré
             * - un blocking_executor pour exécuter les requêtes hors reactor
             */
            if (!resources.mysql_pool) {
                throw std::runtime_error(
                    "RepositoryFactory: missing mysql_pool for a MySQL configuration."
                    );
            }

            if (!blocking_executor) {
                throw std::runtime_error(
                    "RepositoryFactory: missing blocking_executor for a MySQL configuration."
                    );
            }

            return std::make_shared<mysql::MySQLGenericRepository>(
                *resources.mysql_pool,
                std::move(schema_registry),
                std::move(blocking_executor)
                );
        }

        throw sea::sea_errors_handling::PersistenceException("Unknown database type.");
    } catch (const sea::sea_errors_handling::PersistenceException& e){
        return std::shared_ptr<IGenericRepository>();
    }
}

} // namespace sea::infrastructure::persistence