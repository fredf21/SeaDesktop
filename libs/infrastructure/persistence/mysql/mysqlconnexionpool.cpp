#include "mysqlconnexionpool.h"

#include <stdexcept>
#include <utility>

namespace sea::infrastructure::persistence::mysql {

MysqlConnexionPool::MysqlConnexionPool(
    MySQLConnector connector,
    std::size_t pool_size,
    std::shared_ptr<IBlockingExecutor> executor)
    : _connector(std::move(connector))
    , _sem(0)
    , _pool_size(pool_size)
    , _executor(std::move(executor))
{
    if (!_executor) {
        throw std::runtime_error("MysqlConnexionPool: executor manquant.");
    }
}

seastar::future<>
MysqlConnexionPool::start()
{
    if (_pool_size == 0) {
        throw std::runtime_error("MysqlConnexionPool: pool_size must be > 0");
    }

    /**
     * On crée les connexions une par une.
     *
     * La création réelle est faite dans le thread pool parce que :
     * - MySQL Connector/C++ est bloquant
     * - la négociation TCP/SSL peut passer par libcrypto
     * - cela peut provoquer des reactor stalls si exécuté dans Seastar
     */
    for (std::size_t i = 0; i < _pool_size; ++i) {
        auto conn = co_await _executor->submit([this] {
            return _connector.createConnection();
        });

        _available.push(conn.get());
        _connections.push_back(std::move(conn));
    }

    /**
     * Maintenant que toutes les connexions sont disponibles,
     * on libère le sémaphore.
     */
    _sem.signal(_pool_size);

    co_return;
}

seastar::future<>
MysqlConnexionPool::stop()
{
    /**
     * On vide d'abord la queue côté reactor.
     * Elle contient seulement des pointeurs non-owning.
     */
    while (!_available.empty()) {
        _available.pop();
    }

    /*
     * On ne déplace pas le vector<unique_ptr> dans la lambda.
     * std::function exige une callable copiable.
     *
     * On détruit donc les connexions ici.
     * C’est acceptable au shutdown.
     */
    _connections.clear();

    co_return;
}

seastar::future<sql::Connection*>
MysqlConnexionPool::acquire()
{
    /**
     * Attend qu'une connexion soit disponible.
     *
     * Cette attente est non bloquante pour Seastar.
     */
    co_await _sem.wait(1);

    auto* conn = _available.front();
    _available.pop();

    co_return conn;
}

void MysqlConnexionPool::release(sql::Connection* conn)
{
    if (!conn) {
        return;
    }

    /**
     * Remet la connexion dans la queue.
     *
     * Important :
     * release doit être appelé depuis le reactor, pas depuis le worker thread.
     */
    _available.push(conn);
    _sem.signal(1);
}

} // namespace sea::infrastructure::persistence::mysql