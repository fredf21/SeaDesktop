#include "mysqlconnexionpool.h"
#include <seastar/core/thread.hh>
namespace sea::infrastructure::persistence::mysql {

MysqlConnexionPool::MysqlConnexionPool(MySQLConnector connector, size_t pool_size) : _connector(connector), _pool_size(pool_size), _sem(0)
{

}

seastar::future<> MysqlConnexionPool::start()
{
    // std::cerr << "[MYSQL POOL] start pool size=" << _pool_size << "\n";
    return seastar::async([this] {
        for (size_t i = 0; i < _pool_size; ++i) {
            auto conn = _connector.createConnection();
            _available.push(conn.get());
            _connections.push_back(std::move(conn));
        }
        // std::cerr << "[MYSQL POOL] signaling " << _pool_size << "\n";
        _sem.signal(_pool_size);
    });
}

seastar::future<> MysqlConnexionPool::stop()
{
    return seastar::async([this] {
        while (!_available.empty()) _available.pop();
        _connections.clear();
    });
}

seastar::future<sql::Connection*> MysqlConnexionPool::acquire()
{
    // std::cerr << "[MYSQL POOL] waiting acquire\n";
    return _sem.wait(1).then([this] {
        // std::cerr << "[MYSQL POOL] acquired\n";
        auto* conn = _available.front();
        _available.pop();
        return conn;
    });
}

void sea::infrastructure::persistence::mysql::MysqlConnexionPool::release(sql::Connection *conn)
{
    // std::cerr << "[MYSQL POOL] release\n";
    if (!conn) {
        return;
    }

    _available.push(conn);
    _sem.signal(1);
}
}