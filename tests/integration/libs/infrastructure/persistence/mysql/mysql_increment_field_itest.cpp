// ═══════════════════════════════════════════════════════════════
// integration/libs/infrastructure/persistence/mysql/
//     mysql_increment_field_itest.cpp
//
// Tests d'INTÉGRATION de MySQLGenericRepository::increment_field.
//
// increment_field exécute :
//   UPDATE `<table>` SET `<field>` = `<field>` + ? WHERE `id` = ...
//
// ── Pourquoi un test de concurrence ────────────────────────────
//
// L'atomicité d'increment_field ne vient PAS du code C++ : elle
// vient du fait que "field = field + ?" est UNE seule instruction
// SQL, qu'InnoDB exécute atomiquement au niveau ligne (verrou de
// ligne implicite le temps de l'UPDATE). Si l'implémentation faisait
// un read-modify-write côté application (SELECT puis UPDATE), des
// incréments concurrents s'écraseraient mutuellement et le total
// serait faux.
//
// Le test central lance N incréments EN PARALLÈLE (seastar::when_all)
// et vérifie que la valeur finale vaut exactement la somme attendue.
// C'est le seul test de toute la suite qui éprouve une vraie race.
//
// ── Particularités lues dans le code de production ─────────────
//
//   - entity_name est utilisé DIRECTEMENT comme nom de table (pas
//     de passage par le registry). On passe donc "items", pas
//     "Item".
//   - delta est un std::int64_t : peut être négatif (décrément).
//   - Retour : true si exactement 1 ligne affectée. id inexistant
//     → 0 ligne → false. identifiant SQL invalide → false.
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
#include <seastar/core/loop.hh>
#include <seastar/core/when_all.hh>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace mysql   = sea::infrastructure::persistence::mysql;
namespace runtime = sea::infrastructure::runtime;

// Nom de la TABLE (pas de l'entité) : increment_field utilise son
// argument entity_name directement comme nom de table.
constexpr const char* kItemsTable = "items";

// ───────────────────────────────────────────────────────────────
// Schéma : Item(id UUID, name String, counter BigInt). Le champ
// "counter" est l'objet des incréments. BigInt pour avoir une marge
// confortable et un type signé côté domaine.
// ───────────────────────────────────────────────────────────────
sea::domain::Schema build_counter_schema()
{
    using sea::domain::Entity;
    using sea::domain::Field;
    using sea::domain::FieldType;

    Field id_field;
    id_field.name = "id";
    id_field.type = FieldType::UUID;
    id_field.required = true;

    Field name_field;
    name_field.name = "name";
    name_field.type = FieldType::String;
    name_field.required = true;

    Field counter_field;
    counter_field.name = "counter";
    counter_field.type = FieldType::BigInt;
    counter_field.required = false;

    Entity item;
    item.name       = "Item";
    item.table_name = kItemsTable;
    item.fields     = {id_field, name_field, counter_field};

    sea::domain::Schema schema;
    schema.entities = {item};
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
// IncrementTestContext
//
// Base jetable bootstrappée + repository. À construire DANS
// run_on_reactor.
//
// create_item(name, counter) crée une ligne et renvoie son id.
// read_counter(id) relit la valeur courante du champ counter.
// ───────────────────────────────────────────────────────────────
struct IncrementTestContext {
    sea::itest::MysqlTestFixture fixture;
    sea::itest::ScopedDatabase   scoped_db;

    std::unique_ptr<seastar::sharded<mysql::MysqlConnexionPool>> pool;
    std::shared_ptr<runtime::SchemaRuntimeRegistry>             registry;
    std::unique_ptr<mysql::MySQLGenericRepository>              repository;

    IncrementTestContext()
        : scoped_db(fixture)
    {
        const auto schema = build_counter_schema();
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

    // Crée un Item avec une valeur counter initiale, renvoie son id.
    std::string create_item(const std::string& name, std::int64_t counter) {
        runtime::DynamicRecord rec;
        rec["name"]    = name;
        rec["counter"] = counter;
        const auto created = repository->create("Item", rec).get();
        REQUIRE(created.has_value());
        return std::get<std::string>(created->at("id"));
    }

    // Relit la valeur courante du champ counter pour l'item donné.
    // counter est un BigInt → relu en std::int64_t (signé) côté
    // repository.
    std::int64_t read_counter(const std::string& id) {
        const auto rec = repository->find_by_id("Item", id).get();
        REQUIRE(rec.has_value());
        const auto it = rec->find("counter");
        REQUIRE(it != rec->end());
        // BigInt signé → int64_t. (Si la colonne était unsigned, le
        // repository renverrait uint64_t — ce n'est pas le cas ici.)
        REQUIRE(std::holds_alternative<std::int64_t>(it->second));
        return std::get<std::int64_t>(it->second);
    }

    // Destructeur RAII : arrête le pool. Garanti même si un test
    // échoue en plein milieu — une exception REQUIRE sauterait
    // par-dessus tout appel teardown() explicite, et le
    // sharded<MysqlConnexionPool> détruit sans stop() déclenche
    // l'assertion Seastar `_instances.empty()` (SIGABRT).
    ~IncrementTestContext() {
        if (pool) {
            try {
                pool->stop().get();
            } catch (...) {
                // Best-effort : un échec d'arrêt ne doit pas
                // masquer le résultat du test.
            }
        }
    }

    IncrementTestContext(const IncrementTestContext&)            = delete;
    IncrementTestContext& operator=(const IncrementTestContext&) = delete;
};

} // namespace


// ═══════════════════════════════════════════════════════════════
TEST_SUITE("MySQLGenericRepository::increment_field [integration]") {

    // ───────────────────────────────────────────────────────────────
    // Incrément simple : counter 0 + delta 5 → 7 attendu après deux
    // appels (0 +5 +2). On vérifie la valeur RÉELLE relue en base.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("increment_field ajoute le delta à la valeur courante") {
        sea::itest::run_on_reactor([] {
            IncrementTestContext ctx;
            const auto id = ctx.create_item("compteur", 0);

            const bool ok1 =
                ctx.repository->increment_field(kItemsTable, id, "counter", 5).get();
            CHECK(ok1);
            CHECK(ctx.read_counter(id) == 5);

            const bool ok2 =
                ctx.repository->increment_field(kItemsTable, id, "counter", 2).get();
            CHECK(ok2);
            CHECK(ctx.read_counter(id) == 7);

        });
    }

    // ───────────────────────────────────────────────────────────────
    // Delta négatif : increment_field doit aussi savoir décrémenter
    // (delta est un int64_t signé).
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("increment_field accepte un delta négatif (décrément)") {
        sea::itest::run_on_reactor([] {
            IncrementTestContext ctx;
            const auto id = ctx.create_item("compteur", 100);

            const bool ok =
                ctx.repository->increment_field(kItemsTable, id, "counter", -30).get();
            CHECK(ok);
            CHECK(ctx.read_counter(id) == 70);

        });
    }

    // ───────────────────────────────────────────────────────────────
    // id inexistant : aucune ligne affectée → increment_field renvoie
    // false (et ne lève pas).
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("increment_field renvoie false pour un id inexistant") {
        sea::itest::run_on_reactor([] {
            IncrementTestContext ctx;
            ctx.create_item("présent", 0);   // une ligne existe, mais pas celle-ci

            const std::string absent = "00000000-0000-0000-0000-0000000000ff";

            bool ok = true;
            REQUIRE_NOTHROW(
                ok = ctx.repository->increment_field(
                                       kItemsTable, absent, "counter", 1).get()
                );
            CHECK_FALSE(ok);

        });
    }

    // ───────────────────────────────────────────────────────────────
    // Champ invalide comme identifiant SQL : doit renvoyer false sans
    // lever, et ne RIEN modifier en base.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("increment_field rejette un field_name invalide sans rien modifier") {
        sea::itest::run_on_reactor([] {
            IncrementTestContext ctx;
            const auto id = ctx.create_item("compteur", 42);

            bool ok = true;
            REQUIRE_NOTHROW(
                ok = ctx.repository->increment_field(
                                       kItemsTable, id, "counter` = counter; DROP TABLE items;--", 1).get()
                );
            CHECK_FALSE(ok);

            // La valeur ne doit pas avoir bougé, et la table doit être
            // intacte avec sa ligne.
            CHECK(ctx.read_counter(id) == 42);
            CHECK(ctx.repository->count("Item").get() == 1);

        });
    }

    // ───────────────────────────────────────────────────────────────
    // LE test central : CONCURRENCE.
    //
    // On lance 100 incréments de +1 EN PARALLÈLE sur la même ligne, via
    // seastar::when_all. Si "field = field + ?" est bien atomique au
    // niveau SQL, la valeur finale vaut exactement 100. Si l'opération
    // faisait un read-modify-write non atomique, des incréments
    // seraient perdus et le total serait < 100.
    //
    // Note Seastar : sur 1 shard, les 100 futures sont lancées puis
    // progressent de façon entrelacée ; chacune part dans l'executor
    // (thread pool) pour son appel MySQL bloquant. Plusieurs UPDATE
    // peuvent donc être réellement en vol simultanément côté MySQL —
    // c'est exactement la situation de course que l'on veut éprouver.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("increment_field : 100 incréments concurrents donnent un total exact") {
        sea::itest::run_on_reactor([] {
            IncrementTestContext ctx;
            const auto id = ctx.create_item("compteur-concurrent", 0);

            constexpr int kIncrements = 100;

            // Lance les 100 incréments sans les attendre un par un :
            // on collecte les futures, puis on attend le tout.
            std::vector<seastar::future<bool>> futures;
            futures.reserve(kIncrements);
            for (int i = 0; i < kIncrements; ++i) {
                futures.push_back(
                    ctx.repository->increment_field(kItemsTable, id, "counter", 1));
            }

            // when_all attend la complétion de toutes les futures.
            auto results = seastar::when_all(
                               futures.begin(), futures.end()).get();

            // Chaque incrément doit avoir réussi (1 ligne affectée).
            int success_count = 0;
            for (auto& f : results) {
                if (f.get()) {
                    ++success_count;
                }
            }
            CHECK(success_count == kIncrements);

            // LE point décisif : la valeur finale relue en base. Elle
            // doit valoir EXACTEMENT 100. Toute valeur inférieure
            // signifierait des incréments perdus par une race.
            CHECK(ctx.read_counter(id) == kIncrements);

        });
    }

    // ───────────────────────────────────────────────────────────────
    // Concurrence avec deltas mixtes : 50 fois +2 et 50 fois -1, en
    // parallèle. Total attendu : 50*2 - 50*1 = 50. Vérifie que
    // l'atomicité tient aussi quand les deltas diffèrent.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("increment_field : incréments et décréments concurrents se cumulent juste") {
        sea::itest::run_on_reactor([] {
            IncrementTestContext ctx;
            const auto id = ctx.create_item("compteur-mixte", 0);

            std::vector<seastar::future<bool>> futures;
            futures.reserve(100);

            for (int i = 0; i < 50; ++i) {
                futures.push_back(
                    ctx.repository->increment_field(kItemsTable, id, "counter", 2));
            }
            for (int i = 0; i < 50; ++i) {
                futures.push_back(
                    ctx.repository->increment_field(kItemsTable, id, "counter", -1));
            }

            auto results = seastar::when_all(
                               futures.begin(), futures.end()).get();
            for (auto& f : results) {
                CHECK(f.get());
            }

            // 50*(+2) + 50*(-1) = 100 - 50 = 50.
            CHECK(ctx.read_counter(id) == 50);

        });
    }

} // TEST_SUITE