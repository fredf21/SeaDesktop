// ═══════════════════════════════════════════════════════════════
// integration/support/mysql_test_fixture.cpp
//
// Implémentation de la fixture MySQL jetable. Voir le header pour
// le contrat et le cycle de vie.
// ═══════════════════════════════════════════════════════════════

#include "mysql_test_fixture.h"

#include "thread_pool_execution/std_thread_pool_executor.h"

// Connexion directe au driver, SANS passer par MySQLConnector.
//
// Raison : MySQLConnector::createConnection() inclut toujours la base
// dans l'URL (format MariaDB Connector). Or pour CREATE/DROP DATABASE
// on doit se connecter SANS base selectionnee (la base jetable n'existe
// pas encore au moment du CREATE). MariaDB rejette une URL vide avec
// "No database selected".
//
// C'est exactement le choix fait par MysqlBootstrapper, qui pour son
// propre CREATE DATABASE appelle driver->connect(...) directement
// sans base. La fixture calque ce pattern : on reste coherent
// avec le code de production et on ne le modifie pas.

#include <mariadb/conncpp.hpp>

#include <seastar/core/future.hh>
#include <seastar/core/loop.hh>

#include <atomic>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace sea::itest {

namespace {

// Lit une variable d'environnement, ou renvoie `fallback` si absente
// ou vide.
std::string env_or(const char* name, const std::string& fallback)
{
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    return std::string{value};
}

// ───────────────────────────────────────────────────────────────
// connect_without_schema
//
// Ouvre une connexion MySQL SANS sélectionner de base (pas de
// setSchema). Indispensable pour CREATE/DROP DATABASE : la base
// jetable n'existe pas encore au moment du CREATE.
//
// Calque le pattern de MysqlBootstrapper::ensure_database_exists,
// qui se connecte de la même façon (driver->connect sans setSchema).
//
// Appel bloquant cppconn : à n'invoquer QUE depuis l'executor.
// ───────────────────────────────────────────────────────────────
std::unique_ptr<sql::Connection>
connect_without_schema(const sea::itest::MysqlConnectionParams& params)
{
    std::ostringstream url;
    url << "tcp://" << params.host << ":" << params.port;

    sql::Driver* driver = sql::mariadb::get_driver_instance();
    return std::unique_ptr<sql::Connection>(
        driver->connect(url.str(), params.user, params.password));
}

} // namespace

// ───────────────────────────────────────────────────────────────
// MysqlConnectionParams::from_environment
//
// Défauts strictement alignés sur tests/docker-compose.test.yml.
// ───────────────────────────────────────────────────────────────
MysqlConnectionParams MysqlConnectionParams::from_environment()
{
    MysqlConnectionParams params;

    params.host     = env_or("SEA_ITEST_DB_HOST",     "127.0.0.1");
    params.user     = env_or("SEA_ITEST_DB_USER",     "sea_itest");
    params.password = env_or("SEA_ITEST_DB_PASSWORD", "sea_itest_pwd");

    const std::string port_str = env_or("SEA_ITEST_DB_PORT", "13306");
    try {
        params.port = static_cast<unsigned int>(std::stoul(port_str));
    } catch (const std::exception&) {
        throw std::runtime_error(
            "SEA_ITEST_DB_PORT invalide: '" + port_str + "'");
    }

    return params;
}

// ───────────────────────────────────────────────────────────────
// MysqlTestFixture::next_id
//
// Compteur atomique : unicité même si plusieurs fixtures coexistent.
// ───────────────────────────────────────────────────────────────
std::uint64_t MysqlTestFixture::next_id() noexcept
{
    static std::atomic<std::uint64_t> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

// ───────────────────────────────────────────────────────────────
// Constructeur : aucune I/O. On résout juste params + nom de base.
//
// Le nom inclut le PID et un compteur, pour qu'aucune collision ne
// soit possible entre runs concurrents ou entre fixtures du même run.
// Le préfixe "seadesktop_itest_" correspond exactement au motif
// couvert par le GRANT de tests/itest-init/01-grant.sql.
// ───────────────────────────────────────────────────────────────
MysqlTestFixture::MysqlTestFixture()
    : params_(MysqlConnectionParams::from_environment())
{
    database_name_ = "seadesktop_itest_"
                     + std::to_string(static_cast<long>(::getpid()))
                     + "_"
                     + std::to_string(next_id());

    // Un petit pool de threads suffit : les tests ne sont pas une
    // charge de production. 4 threads couvrent largement le besoin.
    executor_ = std::make_shared<StdThreadPoolExecutor>(4);
}

// ───────────────────────────────────────────────────────────────
// create_database
//
// Se connecte SANS schéma (la base jetable n'existe pas encore),
// puis exécute CREATE DATABASE. L'appel bloquant MySQL est déporté
// dans l'executor via executor_->submit, comme partout ailleurs.
// ───────────────────────────────────────────────────────────────
seastar::future<> MysqlTestFixture::create_database()
{
    auto params = params_;
    auto db     = database_name_;

    return executor_->submit([params, db]() {
        // Connexion SANS base sélectionnée : la base jetable n'existe
        // pas encore, donc aucun setSchema (cf. connect_without_schema).
        std::unique_ptr<sql::Connection> conn = connect_without_schema(params);
        std::unique_ptr<sql::Statement>  stmt{conn->createStatement()};

        // Backticks autour du nom : il contient des underscores mais
        // reste un identifiant sûr (généré par nous, jamais saisi).
        stmt->execute("CREATE DATABASE IF NOT EXISTS `" + db + "`");
    });
}

// ───────────────────────────────────────────────────────────────
// drop_database
//
// DROP IF EXISTS : sûr même si create_database n'a jamais été
// appelée ou a échoué en amont.
// ───────────────────────────────────────────────────────────────
seastar::future<> MysqlTestFixture::drop_database()
{
    auto params = params_;
    auto db     = database_name_;

    return executor_->submit([params, db]() {
        // Connexion SANS base sélectionnée : si la base à droper
        // n'existe pas/plus, un setSchema dessus échouerait — on
        // l'évite donc, comme pour create_database.
        std::unique_ptr<sql::Connection> conn = connect_without_schema(params);
        std::unique_ptr<sql::Statement>  stmt{conn->createStatement()};

        stmt->execute("DROP DATABASE IF EXISTS `" + db + "`");
    });
}

// ───────────────────────────────────────────────────────────────
// make_pool
//
// Construit un sharded<MysqlConnexionPool> branché sur la base
// jetable. On utilise sharded::start(...) : Seastar instancie un
// MysqlConnexionPool par shard, en transmettant les arguments du
// constructeur (connector, pool_size, executor).
//
// Puis on appelle .invoke_on_all(&MysqlConnexionPool::start) pour
// que chaque shard ouvre réellement ses connexions.
// ───────────────────────────────────────────────────────────────
seastar::future<std::unique_ptr<seastar::sharded<
    sea::infrastructure::persistence::mysql::MysqlConnexionPool>>>
MysqlTestFixture::make_pool(std::size_t pool_size)
{
    using Pool = sea::infrastructure::persistence::mysql::MysqlConnexionPool;

    auto params   = params_;
    auto db       = database_name_;
    auto executor = executor_;

    auto sharded_pool = std::make_unique<seastar::sharded<Pool>>();

    // Connector visant la base jetable : cette fois `database` est
    // renseignée, donc setSchema sélectionnera bien la base.
    sea::infrastructure::persistence::mysql::MySQLConnector connector{params.host, params.user, params.password, db, params.port};

    auto* raw = sharded_pool.get();

    return raw->start(connector, pool_size, executor)
        .then([raw] {
            // Ouvre les connexions sur chaque shard.
            return raw->invoke_on_all(&Pool::start);
        })
        .then([sharded_pool = std::move(sharded_pool)]() mutable {
            return std::move(sharded_pool);
        });
}

// ───────────────────────────────────────────────────────────────
// ScopedDatabase — RAII
//
// Constructeur et destructeur appellent .get() : ils DOIVENT donc
// être exécutés dans un seastar::thread, c'est-à-dire à l'intérieur
// de run_on_reactor. Voir le header.
// ───────────────────────────────────────────────────────────────
ScopedDatabase::ScopedDatabase(MysqlTestFixture& fixture)
    : fixture_(fixture)
{
    fixture_.create_database().get();
}

ScopedDatabase::~ScopedDatabase()
{
    // Le destructeur ne doit jamais laisser fuir une exception.
    // Si le DROP échoue (MySQL coupé, etc.), on absorbe : la base
    // vit en tmpfs et disparaîtra de toute façon à l'arrêt du
    // conteneur.
    try {
        fixture_.drop_database().get();
    } catch (...) {
        // Volontairement silencieux : un échec de nettoyage ne doit
        // pas masquer le résultat réel du test.
    }
}

} // namespace sea::itest