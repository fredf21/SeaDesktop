#pragma once
#include <string>
#include <memory>
#include <mysql_driver.h>
#include <mysql_connection.h>
#include <cppconn/exception.h>
#include <sstream>
#include <stdexcept>

namespace sea::infrastructure::persistence::mysql {

class MySQLConnector{
public:

    MySQLConnector(std::string host,
                   std::string user,
                   std::string password,
                   std::string database,
                   unsigned int port = 3306): _host(std::move(host)), _user(std::move(user)), _password(std::move(password)), _database(std::move(database)), _port(port){};

    //Fonction de connection a la base de donnees Mysql
    std::unique_ptr<sql::Connection> createConnection() const {

        try {
            sql::mysql::MySQL_Driver* driver = sql::mysql::get_mysql_driver_instance();

            std::ostringstream url;
            url << "tcp://" << _host << ":" << _port;

            std::unique_ptr<sql::Connection> connection(
                driver->connect(url.str(), _user, _password)
                );

            connection->setSchema(_database);
            return connection;
        } catch (const sql::SQLException& ex) {
            throw std::runtime_error(
                "Erreur connexion MySQL: " + std::string(ex.what())
                );
        }
    };

private:
    std::string _host;
    std::string _user;
    std::string _password;
    std::string _database;
    unsigned int _port;
};

} // namespace sea::infrastructure::persistence::mysql
