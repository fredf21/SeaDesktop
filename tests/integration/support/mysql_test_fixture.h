#pragma once

// ═══════════════════════════════════════════════════════════════
// integration/support/mysql_test_fixture.h
//
// Fixture de base de données jetable, pour les tests d'intégration.
//
// Analogie : c'est l'équivalent MySQL de QTemporaryDir. Là où le
// QtTest TestFilesystemStorage isole chaque test dans un dossier
// temporaire détruit ensuite, cette fixture isole un test (ou un
// groupe de tests) dans une BASE MySQL fabriquée à la volée et
// supprimée à la fin.
//
// ── Cycle de vie ───────────────────────────────────────────────
//
//   MysqlTestFixture fixture;          // (1) construit la config
//   fixture.create_database().get();   // (2) CREATE DATABASE jetable
//   auto pool = fixture.make_pool();   // (3) pool branché sur elle
//   ...                                //     -> exécuter les tests
//   fixture.drop_database().get();     // (4) DROP DATABASE
//
//   En pratique on n'appelle pas (2)/(4) à la main : on utilise le
//   helper RAII `ScopedDatabase` (cf. plus bas) qui fait create au
//   constructeur et drop au destructeur, même en cas d'exception
//   (donc même quand une assertion doctest échoue).
//
// ── Paramètres de connexion ────────────────────────────────────
//
// Lus depuis l'environnement, avec repli sur des valeurs par
// défaut ALIGNÉES sur tests/docker-compose.test.yml :
//
//   SEA_ITEST_DB_HOST      défaut "127.0.0.1"
//   SEA_ITEST_DB_PORT      défaut 13306
//   SEA_ITEST_DB_USER      défaut "sea_itest"
//   SEA_ITEST_DB_PASSWORD  défaut "sea_itest_pwd"
//
// Conséquence : sans config, les tests visent directement le
// conteneur du docker-compose. En CI, on surcharge via variables
// d'environnement sans recompiler.
//
// ── Nommage des bases jetables ─────────────────────────────────
//
// Chaque base s'appelle  seadesktop_itest_<pid>_<compteur>  afin
// que deux exécutions concurrentes (ou un test qui échoue sans
// nettoyer) n'entrent jamais en collision. Le préfixe
// "seadesktop_itest_" est exactement celui couvert par le GRANT
// du script tests/itest-init/01-grant.sql.
// ═══════════════════════════════════════════════════════════════

#include "thread_pool_execution/i_blocking_executor.h"
#include "persistence/mysql/mysqlconnexionpool.h"

#include <seastar/core/future.hh>
#include <seastar/core/sharded.hh>

#include <cstdint>
#include <memory>
#include <string>

namespace sea::itest {

// ───────────────────────────────────────────────────────────────
// MysqlConnectionParams
//
// Coordonnées de connexion résolues (environnement ou défauts).
// ───────────────────────────────────────────────────────────────
struct MysqlConnectionParams {
    std::string  host;
    unsigned int port;
    std::string  user;
    std::string  password;

    // Résout les paramètres depuis l'environnement, avec repli sur
    // les valeurs par défaut alignées sur le docker-compose.
    static MysqlConnectionParams from_environment();
};

// ───────────────────────────────────────────────────────────────
// MysqlTestFixture
//
// Fabrique une base jetable et fournit de quoi s'y connecter.
//
// Non copiable : la fixture porte le nom d'UNE base jetable
// précise ; la dupliquer n'aurait pas de sens.
// ───────────────────────────────────────────────────────────────
class MysqlTestFixture {
public:
    // Construit la fixture : résout les paramètres de connexion et
    // calcule un nom de base jetable unique. Ne touche PAS encore
    // à MySQL — aucune I/O dans le constructeur.
    MysqlTestFixture();

    MysqlTestFixture(const MysqlTestFixture&)            = delete;
    MysqlTestFixture& operator=(const MysqlTestFixture&) = delete;

    // Nom de la base jetable attribuée à cette fixture.
    [[nodiscard]] const std::string& database_name() const noexcept {
        return database_name_;
    }

    [[nodiscard]] const MysqlConnectionParams& params() const noexcept {
        return params_;
    }

    // ── Gestion de la base jetable ─────────────────────────────

    // CREATE DATABASE `<database_name_>`.
    // Se connecte SANS schéma sélectionné (la base n'existe pas
    // encore). Idempotent : utilise IF NOT EXISTS.
    seastar::future<> create_database();

    // DROP DATABASE IF EXISTS `<database_name_>`.
    // Sûr à appeler même si la base n'a jamais été créée.
    seastar::future<> drop_database();

    // ── Construction d'un pool branché sur la base jetable ─────

    // Démarre un seastar::sharded<MysqlConnexionPool> connecté à la
    // base jetable, prêt à être passé à un MySQLGenericRepository
    // ou à un MysqlBootstrapper.
    //
    // L'appelant est responsable d'appeler stop() sur le sharded
    // renvoyé avant de le détruire (cf. ScopedPool pour automatiser).
    //
    // pool_size : nombre de connexions par shard (défaut 2, suffisant
    //             pour des tests ; les tests tournent en 1 shard).
    seastar::future<std::unique_ptr<seastar::sharded<
        sea::infrastructure::persistence::mysql::MysqlConnexionPool>>>
    make_pool(std::size_t pool_size = 2);

    // Executor bloquant partagé par la fixture. Exposé pour que les
    // tests puissent le passer aux composants qui en exigent un
    // (MysqlBootstrapper, MySQLGenericRepository).
    [[nodiscard]] std::shared_ptr<IBlockingExecutor> executor() const noexcept {
        return executor_;
    }

private:
    MysqlConnectionParams params_;
    std::string           database_name_;

    // Executor dédié aux appels MySQL bloquants. Partagé par tout
    // ce que la fixture fabrique (pool inclus).
    std::shared_ptr<IBlockingExecutor> executor_;

    // Compteur process-global garantissant l'unicité des noms de
    // base même si plusieurs fixtures sont créées dans un run.
    static std::uint64_t next_id() noexcept;
};

// ───────────────────────────────────────────────────────────────
// ScopedDatabase
//
// Garde RAII : create_database() à la construction, drop_database()
// à la destruction — y compris si une exception traverse (donc y
// compris quand une assertion doctest échoue).
//
// À utiliser DANS run_on_reactor, car le constructeur et le
// destructeur appellent .get() sur des futures Seastar.
//
//   sea::itest::run_on_reactor([] {
//       MysqlTestFixture fixture;
//       MysqlTestFixture::ScopedDatabase db{fixture};   // CREATE
//       // ... tests ...
//   }); // <- DROP ici, garanti
// ───────────────────────────────────────────────────────────────
class ScopedDatabase {
public:
    explicit ScopedDatabase(MysqlTestFixture& fixture);
    ~ScopedDatabase();

    ScopedDatabase(const ScopedDatabase&)            = delete;
    ScopedDatabase& operator=(const ScopedDatabase&) = delete;

private:
    MysqlTestFixture& fixture_;
};

} // namespace sea::itest
