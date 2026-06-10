#include "mysqlconnexionpool.h"
#include "exception_handling.h"
#include "spdlog/spdlog.h"

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
        throw std::runtime_error("MysqlConnexionPool: Missing executor.");
    }
}

seastar::future<>
MysqlConnexionPool::start()
{
    try{
        if (_pool_size == 0) {
            throw sea::sea_errors_handling::PersistenceException("MysqlConnexionPool: pool_size must be > 0");
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
    } catch(const sea::sea_errors_handling::PersistenceException& e){

    }
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
    co_await _sem.wait(1);
    auto* conn = _available.front();
    _available.pop();

    // Health check léger : si la connexion est explicitement fermée
    // (timeout MySQL, server gone away), on la remplace avant de la
    // rendre au caller. isClosed() est local et ne fait pas d'I/O.
    bool closed = false;
    try {
        closed = conn->isClosed();
    } catch (...) {
        // isClosed() ne devrait pas throw, mais par sécurité.
        closed = true;
    }

    if (closed) {
        co_await discard_and_replace(conn);
        // Re-acquire (on a juste signalé le sémaphore dans
        // discard_and_replace, donc wait(1) passe immédiatement).
        co_await _sem.wait(1);
        conn = _available.front();
        _available.pop();
    }

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

seastar::future<>
MysqlConnexionPool::discard_and_replace(sql::Connection* conn)
{
    if (!conn) {
        spdlog::get("sea.persistence")->warn(
            "discard_and_replace called with nullptr"
            );
    }

    // 1. Trouve l'unique_ptr proprietaire dans _connections, et
    //    le sort du vector.
    std::unique_ptr<sql::Connection> dead;
    if (conn) {
        for (auto it = _connections.begin();
             it != _connections.end(); ++it)
        {
            if (it->get() == conn) {
                dead = std::move(*it);
                _connections.erase(it);
                break;
            }
        }
        if (!dead) {
            spdlog::get("sea.persistence")->warn(
                "discard_and_replace: connection not found in pool"
                );
        }
    }

    // 2. Volontairement, on NE detruit PAS la connexion morte.
    //
    // Raison : libmysqlclient 21 crash (SEGFAULT dans son cleanup
    // interne) quand on detruit une connexion qui a recu une
    // "Lost connection to MySQL server during query" cote client.
    // Le destructeur ~MySQL_Connection essaie de close() proprement
    // mais le state interne est corrompu → crash.
    //
    // On accepte le leak (~quelques KB par connexion morte) pour
    // garder le serveur stable. En pratique, ca n'arrive que sous
    // saturation MySQL ou si le serveur est tue, donc le volume
    // reste limite.
    //
    // Le unique_ptr `dead` sort de scope ici sans appeler delete
    // sur son contenu : on lui retire la propriete via .release().
    if (dead) {
        sql::Connection* leaked = dead.release();
        (void)leaked;  // intentionnellement non delete
        spdlog::get("sea.persistence")->warn(
            "Leaking dead MySQL connection to avoid libmysqlclient crash"
            );
    }

    // 3. Cree la nouvelle connexion hors reactor. La lambda ne
    //    capture que `this` (copiable), et retourne le unique_ptr
    //    par valeur (move-return : pas de souci de copiabilite
    //    sur le RETOUR, seulement sur la capture).
    std::unique_ptr<sql::Connection> fresh =
        co_await _executor->submit(
            [this]() {
                return _connector.createConnection();
            }
            );

    // 4. Enregistre la nouvelle et signale.
    auto* raw_fresh = fresh.get();
    _connections.push_back(std::move(fresh));
    _available.push(raw_fresh);
    _sem.signal(1);

    spdlog::get("sea.persistence")->debug(
        "Connection replaced (pool size: {})", _connections.size()
        );

    co_return;
}
} // namespace sea::infrastructure::persistence::mysql