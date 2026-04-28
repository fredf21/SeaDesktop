#pragma once

#include "i_generic_repository.h"
#include "database_config.h"
#include "runtime/schema_runtime_registry.h"
#include "mysql/mysqlconnexionpool.h"
#include "thread_pool_execution/i_blocking_executor.h"

#include <seastar/core/sharded.hh>

#include <memory>

namespace sea::infrastructure::persistence {

class RepositoryFactory {
public:
    /**
     * Ressources externes déjà initialisées.
     *
     * Exemple :
     * - mysql_pool est créé et démarré dans main.cpp.
     * - RepositoryFactory ne possède pas ces ressources, il les injecte seulement.
     */
    struct DatabaseResources {
        seastar::sharded<mysql::MysqlConnexionPool>* mysql_pool = nullptr;
    };

    /**
     * Crée un repository basé sur la configuration de base de données.
     *
     * @param config Configuration de la base choisie dans le YAML.
     * @param schema_registry Registre runtime des entités.
     * @param resources Ressources de base déjà démarrées.
     * @param blocking_executor Executor utilisé pour déplacer les opérations bloquantes
     *        hors du reactor Seastar.
     */
    std::shared_ptr<IGenericRepository>
    create(const sea::domain::DatabaseConfig& config,
           std::shared_ptr<runtime::SchemaRuntimeRegistry> schema_registry,
           const DatabaseResources& resources,
           std::shared_ptr<IBlockingExecutor> blocking_executor) const;
};

} // namespace sea::infrastructure::persistence