#pragma once

#include "mysql_connector.h"
#include "thread_pool_execution/i_blocking_executor.h"

#include <cppconn/connection.h>

#include <memory>
#include <queue>
#include <type_traits>
#include <utility>
#include <vector>

#include <seastar/core/future.hh>
#include <seastar/core/semaphore.hh>

namespace sea::infrastructure::persistence::mysql {

/**
 * Pool de connexions MySQL.
 *
 * Important :
 * MySQL Connector/C++ est bloquant.
 * Donc la création de connexions et l'exécution de requêtes SQL
 * doivent passer par IBlockingExecutor.
 */
class MysqlConnexionPool
{
public:
    MysqlConnexionPool(
        MySQLConnector connector,
        std::size_t pool_size,
        std::shared_ptr<IBlockingExecutor> executor
        );

    /**
     * Crée les connexions MySQL.
     *
     * Les appels bloquants à MySQL sont exécutés dans le thread pool.
     */
    seastar::future<> start();

    /**
     * Ferme/nettoie les connexions.
     *
     * La destruction des connexions MySQL peut aussi être coûteuse,
     * donc on la déplace hors reactor.
     */
    seastar::future<> stop();

    /**
     * Réserve une connexion disponible.
     *
     * Cette partie reste côté reactor et ne fait pas d'I/O bloquante.
     */
    seastar::future<sql::Connection*> acquire();

    /**
     * Remet une connexion dans le pool.
     */
    void release(sql::Connection* conn);

    /**
     * Helper optionnel pour exécuter une opération MySQL avec une connexion.
     *
     * La fonction fournie est exécutée dans le blocking executor.
     */
    template <typename Func>
    auto with_connection(Func&& func)
    {
        using Result = std::invoke_result_t<Func, sql::Connection*>;

        return acquire().then(
            [this, func = std::forward<Func>(func)](sql::Connection* conn) mutable {
                return _executor->submit(
                                    [conn, func = std::move(func)]() mutable -> Result {
                                        return func(conn);
                                    }
                                    ).finally([this, conn] {
                        release(conn);
                    });
            }
            );
    }

private:
    MySQLConnector _connector;

    /**
     * Connexions possédées par le pool.
     */
    std::vector<std::unique_ptr<sql::Connection>> _connections;

    /**
     * Pointeurs non-owning vers les connexions disponibles.
     */
    std::queue<sql::Connection*> _available;

    /**
     * Sémaphore Seastar indiquant le nombre de connexions disponibles.
     */
    seastar::semaphore _sem;

    std::size_t _pool_size;

    /**
     * Executor dédié aux appels bloquants MySQL.
     */
    std::shared_ptr<IBlockingExecutor> _executor;
};

} // namespace sea::infrastructure::persistence::mysql