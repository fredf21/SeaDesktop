// ═══════════════════════════════════════════════════════════════
// integration/libs/infrastructure/persistence/mysql/
//     mysql_transaction_itest.cpp
//
// Tests d'INTÉGRATION de MySQLGenericRepository::in_transaction.
//
// in_transaction est le composant le plus critique pour l'intégrité
// des données : c'est lui qui garantit l'atomicité ACID. Ces tests
// ne se contentent donc PAS de regarder TransactionResult.committed —
// ils RELISENT la base après coup pour vérifier l'effet réel :
//   - après COMMIT  : les données doivent être présentes ;
//   - après ROLLBACK : les données NE doivent PAS être présentes.
//
// C'est la seule façon de prouver qu'un ROLLBACK défait réellement
// les écritures, et pas seulement qu'il renvoie committed=false.
//
// Sémantique vérifiée (lue dans mysql_generic_repository.cpp) :
//   - lambda renvoie true       → COMMIT,  committed = true
//   - lambda renvoie false      → ROLLBACK, committed = false
//   - lambda lève une exception → ROLLBACK, committed = false
//
// NOTE IMPORTANTE sur les exceptions : le commentaire d'en-tête du
// code de production annonce "ROLLBACK + rethrow". En réalité le
// code ATTRAPE l'exception et la convertit en
// TransactionResult{committed:false} — il n'y a PAS de rethrow.
// Les tests ci-dessous vérifient le comportement RÉEL (pas de
// rethrow), pas le commentaire.
//
// Pré-requis : MySQL de tests/docker-compose.test.yml en marche.
// ═══════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include "support/seastar_test_harness.h"
#include "support/mysql_test_fixture.h"

#include "persistence/mysql/mysql_generic_repository.h"
#include "persistence/mysql/mysql_bootstrapper.h"

#include "runtime/schema_runtime_registry.h"
#include "runtime/dynamic_record.h"

#include "database_config.h"
#include "schema.h"
#include "entity.h"
#include "field.h"
#include "field_type.h"

#include <seastar/core/future.hh>
#include <seastar/core/coroutine.hh>

#include <memory>
#include <stdexcept>
#include <string>

namespace {

namespace mysql   = sea::infrastructure::persistence::mysql;
namespace runtime = sea::infrastructure::runtime;

// ───────────────────────────────────────────────────────────────
// Schéma et config — mêmes helpers que les autres fichiers de test
// du repository : entité "Product" (id / name / price).
// ───────────────────────────────────────────────────────────────
sea::domain::Schema build_product_schema()
{
    using sea::domain::Entity;
    using sea::domain::Field;
    using sea::domain::FieldType;

    Field id_field;
    id_field.name     = "id";
    id_field.type     = FieldType::UUID;
    id_field.required = true;

    Field name_field;
    name_field.name     = "name";
    name_field.type     = FieldType::String;
    name_field.required = true;

    Field price_field;
    price_field.name     = "price";
    price_field.type     = FieldType::Int;
    price_field.required = false;

    Entity product;
    product.name       = "Product";
    product.table_name = "products";
    product.fields     = {id_field, name_field, price_field};

    sea::domain::Schema schema;
    schema.entities = {product};
    return schema;
}

sea::domain::DatabaseConfig
build_database_config(const sea::itest::MysqlTestFixture& fixture)
{
    sea::domain::DatabaseConfig config;

    config.type          = sea::domain::DatabaseType::MySQL;
    config.host          = fixture.params().host;
    config.port          = static_cast<int>(fixture.params().port);
    config.database_name = fixture.database_name();
    config.username      = fixture.params().user;
    config.password      = fixture.params().password;

    config.migrations.enabled                    = true;
    config.migrations.mode                       = sea::domain::MigrationMode::Conservative;
    config.migrations.create_database_if_missing = true;

    return config;
}

// ───────────────────────────────────────────────────────────────
// TxnTestContext — même structure que RepositoryTestContext :
// base jetable bootstrappée + registry + repository prêts.
// À construire DANS run_on_reactor.
// ───────────────────────────────────────────────────────────────
struct TxnTestContext {
    sea::itest::MysqlTestFixture fixture;
    sea::itest::ScopedDatabase   scoped_db;

    std::unique_ptr<seastar::sharded<mysql::MysqlConnexionPool>> pool;
    std::shared_ptr<runtime::SchemaRuntimeRegistry>             registry;
    std::unique_ptr<mysql::MySQLGenericRepository>              repository;

    TxnTestContext()
        : scoped_db(fixture)
    {
        const auto schema = build_product_schema();
        const auto config = build_database_config(fixture);

        pool = fixture.make_pool().get();

        mysql::MysqlBootstrapper bootstrapper{
                                              config, schema, *pool, fixture.executor()};
        REQUIRE(bootstrapper.bootstrap().get().success);

        registry = std::make_shared<runtime::SchemaRuntimeRegistry>();
        registry->register_schema(schema);

        repository = std::make_unique<mysql::MySQLGenericRepository>(
            *pool, registry, fixture.executor());
    }

    // Destructeur RAII : arrête le pool. Garanti même si un test
    // échoue en plein milieu — une exception REQUIRE sauterait
    // par-dessus tout appel teardown() explicite, et le
    // sharded<MysqlConnexionPool> détruit sans stop() déclenche
    // l'assertion Seastar `_instances.empty()` (SIGABRT).
    ~TxnTestContext() {
        if (pool) {
            try {
                pool->stop().get();
            } catch (...) {
                // Best-effort : un échec d'arrêt ne doit pas
                // masquer le résultat du test.
            }
        }
    }

    TxnTestContext(const TxnTestContext&)            = delete;
    TxnTestContext& operator=(const TxnTestContext&) = delete;
};

// Helper : fabrique un DynamicRecord Product. price est un int32_t
// (FieldType::Int → std::int32_t côté repository).
runtime::DynamicRecord make_product(const std::string& name,
                                    std::int32_t price)
{
    runtime::DynamicRecord record;
    record["name"]  = name;
    record["price"] = price;
    return record;
}

} // namespace


// ═══════════════════════════════════════════════════════════════
TEST_SUITE("MySQLGenericRepository::in_transaction [integration]") {

    // ───────────────────────────────────────────────────────────────
    // COMMIT : une lambda qui crée un record puis renvoie true doit
    // committer. Le record doit être présent APRÈS la transaction.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("lambda renvoyant true committe : le record persiste") {
        sea::itest::run_on_reactor([] {
            TxnTestContext ctx;
            auto& repo = *ctx.repository;

            const auto result = repo.in_transaction(
                                        [&repo]() -> seastar::future<bool> {
                                            co_await repo.create("Product", make_product("Engagé", 10));
                                            co_return true;   // COMMIT
                                        }).get();

            // Le résultat annonce un commit.
            CHECK(result.committed);
            CHECK(result.error_message.empty());

            // Vérification de l'effet RÉEL : le record est bien là.
            CHECK(repo.count("Product").get() == 1);

        });
    }

    // ───────────────────────────────────────────────────────────────
    // ROLLBACK explicite : une lambda qui crée un record puis renvoie
    // false doit ROLLBACK. Le record NE doit PAS exister après coup.
    //
    // C'est LE test central : si le ROLLBACK ne défait pas réellement
    // l'INSERT, count vaudra 1 et ce test échouera. Un in_transaction
    // qui se contenterait de renvoyer committed=false sans rollback
    // effectif serait démasqué ici.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("lambda renvoyant false rollback : le record n'existe pas") {
        sea::itest::run_on_reactor([] {
            TxnTestContext ctx;
            auto& repo = *ctx.repository;

            const auto result = repo.in_transaction(
                                        [&repo]() -> seastar::future<bool> {
                                            co_await repo.create("Product", make_product("Annulé", 20));
                                            co_return false;  // ROLLBACK
                                        }).get();

            CHECK_FALSE(result.committed);

            // Effet réel : l'INSERT a été défait, la table est vide.
            CHECK(repo.count("Product").get() == 0);

        });
    }

    // ───────────────────────────────────────────────────────────────
    // ROLLBACK sur exception : une lambda qui crée un record PUIS lève
    // une exception doit ROLLBACK. Le record ne doit pas exister.
    //
    // Vérifie aussi le comportement RÉEL face à l'exception : le code
    // de production l'attrape (pas de rethrow, malgré le commentaire
    // d'en-tête). Donc in_transaction(...).get() ne doit PAS lever —
    // il doit renvoyer un TransactionResult avec committed=false.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("lambda qui lève une exception rollback sans rethrow") {
        sea::itest::run_on_reactor([] {
            TxnTestContext ctx;
            auto& repo = *ctx.repository;

            // L'appel ne doit pas lever : l'exception est convertie en
            // TransactionResult. On vérifie d'abord ce point avec
            // REQUIRE_NOTHROW, puis le contenu du résultat.
            sea::infrastructure::persistence::TransactionResult result;

            REQUIRE_NOTHROW(
                result = repo.in_transaction(
                                 [&repo]() -> seastar::future<bool> {
                                     co_await repo.create("Product", make_product("Boom", 30));
                                     throw std::runtime_error("échec simulé dans la transaction");
                                     co_return true;  // jamais atteint
                                 }).get()
                );

            // Exception => pas de commit.
            CHECK_FALSE(result.committed);
            // Le message d'erreur doit être renseigné.
            CHECK_FALSE(result.error_message.empty());

            // Effet réel : l'INSERT précédant le throw a été défait.
            CHECK(repo.count("Product").get() == 0);

        });
    }

    // ───────────────────────────────────────────────────────────────
    // Atomicité multi-opérations : une transaction qui insère DEUX
    // records puis rollback ne doit en laisser AUCUN. C'est le cas
    // d'usage réel (ex : Order + OrderLine ensemble).
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("rollback annule TOUTES les écritures de la transaction") {
        sea::itest::run_on_reactor([] {
            TxnTestContext ctx;
            auto& repo = *ctx.repository;

            const auto result = repo.in_transaction(
                                        [&repo]() -> seastar::future<bool> {
                                            co_await repo.create("Product", make_product("Premier", 1));
                                            co_await repo.create("Product", make_product("Second", 2));
                                            co_return false;  // ROLLBACK des deux
                                        }).get();

            CHECK_FALSE(result.committed);

            // Aucun des deux records ne doit subsister.
            CHECK(repo.count("Product").get() == 0);

        });
    }

    // ───────────────────────────────────────────────────────────────
    // COMMIT multi-opérations : le pendant positif du test précédent.
    // Deux insertions + true => les DEUX records persistent.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("commit persiste TOUTES les écritures de la transaction") {
        sea::itest::run_on_reactor([] {
            TxnTestContext ctx;
            auto& repo = *ctx.repository;

            const auto result = repo.in_transaction(
                                        [&repo]() -> seastar::future<bool> {
                                            co_await repo.create("Product", make_product("Alpha", 100));
                                            co_await repo.create("Product", make_product("Beta", 200));
                                            co_return true;   // COMMIT
                                        }).get();

            CHECK(result.committed);
            CHECK(repo.count("Product").get() == 2);

        });
    }

    // ───────────────────────────────────────────────────────────────
    // Isolation entre transactions : un rollback ne doit PAS affecter
    // les données committées par une transaction précédente.
    //
    // Scénario :
    //   1. Transaction A : crée 1 record, COMMIT.
    //   2. Transaction B : crée 1 record, ROLLBACK.
    //   Attendu : il reste exactement 1 record (celui de A).
    //
    // Si le rollback de B effaçait aussi le record de A, ce test
    // l'attraperait.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("un rollback n'efface pas les données déjà committées") {
        sea::itest::run_on_reactor([] {
            TxnTestContext ctx;
            auto& repo = *ctx.repository;

            // Transaction A : committée.
            const auto a = repo.in_transaction(
                                   [&repo]() -> seastar::future<bool> {
                                       co_await repo.create("Product", make_product("Persistant", 1));
                                       co_return true;
                                   }).get();
            REQUIRE(a.committed);
            REQUIRE(repo.count("Product").get() == 1);

            // Transaction B : annulée.
            const auto b = repo.in_transaction(
                                   [&repo]() -> seastar::future<bool> {
                                       co_await repo.create("Product", make_product("Éphémère", 2));
                                       co_return false;
                                   }).get();
            REQUIRE_FALSE(b.committed);

            // Seul le record de A subsiste.
            CHECK(repo.count("Product").get() == 1);

        });
    }

} // TEST_SUITE