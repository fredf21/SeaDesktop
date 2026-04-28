#pragma once

#include <seastar/http/httpd.hh>

#include <memory>

#include "runtime/generic_crud_engine.h"
#include "runtime/schema_runtime_registry.h"
#include "authservice.h"
#include "database_config.h"
#include "thread_pool_execution/i_blocking_executor.h"

namespace sea::http::handlers::auth {

/**
 * 🔐 RegisterHandler
 *
 * Gère la route POST /auth/register
 *
 * Responsabilités :
 * - Parser le body JSON
 * - Valider les champs (email, password)
 * - Vérifier unicité email
 * - Hasher le password (via thread pool)
 * - Créer l'utilisateur
 *
 * Important :
 * Le hash du password est exécuté via blocking_executor
 * pour éviter de bloquer le reactor Seastar.
 */
class RegisterHandler final : public seastar::httpd::handler_base {
public:
    RegisterHandler(
        std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
        std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry> registry,
        std::shared_ptr<sea::application::AuthService> auth_service,
        std::shared_ptr<IBlockingExecutor> blocking_executor,
        sea::domain::DatabaseType db_type
        );

    /**
     * Point d'entrée HTTP
     */
    seastar::future<std::unique_ptr<seastar::http::reply>>
    handle(const seastar::sstring& path,
           std::unique_ptr<seastar::http::request> req,
           std::unique_ptr<seastar::http::reply> rep) override;

private:
    // Accès aux opérations CRUD
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine_;

    // Accès au schéma runtime (validation, parsing)
    std::shared_ptr<sea::infrastructure::runtime::SchemaRuntimeRegistry> registry_;

    // Service d'auth (hash password, JWT, etc.)
    std::shared_ptr<sea::application::AuthService> auth_service_;

    /**
     * Executor pour opérations bloquantes
     *
     * Utilisé pour :
     * - hash_password (bcrypt / argon2)
     *
     * Objectif :
     * éviter les stalls dans le reactor Seastar.
     */
    std::shared_ptr<IBlockingExecutor> blocking_executor_;

    /**
     * Type de base de données
     *
     * Utilisé pour :
     * - générer un ID en mémoire si nécessaire
     */
    sea::domain::DatabaseType db_type_;
};

} // namespace sea::http::handlers::auth