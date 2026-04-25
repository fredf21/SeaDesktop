#pragma once

#include "i_generic_repository.h"
#include "database_config.h"
#include "runtime/schema_runtime_registry.h"
#include "mysql/mysqlconnexionpool.h"

#include <seastar/core/sharded.hh>

#include <memory>

namespace sea::infrastructure::persistence {

class RepositoryFactory {
public:
    /**
     * Crée un repository basé sur la config fournie.
     *
     * @param config La configuration de base de données choisie
     * @param schema_registry Le registre de schémas (shared, vit longtemps)
     * @param mysql_pool Pool MySQL pré-démarré. Ignoré si config.type != MySQL.
     *                   Peut être nullptr pour les backends non-MySQL.
     */
    struct DatabaseResources {
        seastar::sharded<mysql::MysqlConnexionPool>* mysql_pool = nullptr;
        // etc.
    };
    std::shared_ptr<IGenericRepository>
    create(const sea::domain::DatabaseConfig& config,
           std::shared_ptr<runtime::SchemaRuntimeRegistry> schema_registry,
           const DatabaseResources& resources) const;
};

} // namespace sea::infrastructure::persistence