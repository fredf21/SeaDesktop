#pragma once

#include <string>
#include <memory>
#include <sstream>
#include <stdexcept>

// MariaDB Connector/C++ (remplace Oracle's MySQL Connector/C++ pour cause
// de bug de thread-safety dans libmysqlclient sous charge concurrente —
// voir bug 19).
//
// L'API est intentionnellement tres proche de mysql-cppconn :
//   - meme namespace sql::
//   - meme types sql::Connection, sql::PreparedStatement, etc.
//   - meme sql::SQLException
//
// Les seules differences notables a connaitre :
//   - L'initialisation : sql::mariadb::get_driver_instance() au lieu
//     du driver Oracle (get_mysql_driver_instance).
//   - L'URL inclut directement la base de donnees dans le path :
//     "tcp://host:port/db_name" au lieu de setSchema() apres connect.
#include <mariadb/conncpp.hpp>

namespace sea::infrastructure::persistence::mysql {

class MySQLConnector {
public:
    MySQLConnector(std::string host,
                   std::string user,
                   std::string password,
                   std::string database,
                   unsigned int port = 3306)
        : _host(std::move(host))
        , _user(std::move(user))
        , _password(std::move(password))
        , _database(std::move(database))
        , _port(port)
    {}

    std::unique_ptr<sql::Connection> createConnection() const
    {
        try {
            sql::Driver* driver = sql::mariadb::get_driver_instance();

            std::ostringstream url;
            url << "tcp://" << _host << ":" << _port
                << "/" << _database;

            sql::SQLString sql_url(url.str());
            sql::Properties props({
                {"user",     _user},
                {"password", _password}
            });

            return std::unique_ptr<sql::Connection>(
                driver->connect(sql_url, props)
                );
        }
        catch (sql::SQLException& ex) {
            throw std::runtime_error(
                "Erreur connexion MariaDB/MySQL: "
                + std::string(ex.what())
                );
        }
    }

private:
    std::string _host;
    std::string _user;
    std::string _password;
    std::string _database;
    unsigned int _port;
};

} // namespace sea::infrastructure::persistence::mysql