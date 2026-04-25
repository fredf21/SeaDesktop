#pragma once
#include "mysql_connector.h"
#include <queue>
#include <seastar/core/semaphore.hh>
#include <seastar/core/future.hh>

namespace sea::infrastructure::persistence::mysql {

class MysqlConnexionPool
{
public:
    MysqlConnexionPool(MySQLConnector connector, size_t pool_size);

    seastar::future<>start();
    seastar::future<>stop();
    seastar::future<sql::Connection*>acquire();
    void release(sql::Connection* conn);


private:
    MySQLConnector _connector;
    std::vector<std::unique_ptr<sql::Connection>> _connections;
    std::queue<sql::Connection*> _available;
    seastar::semaphore _sem;
    size_t _pool_size;
};

}