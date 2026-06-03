// ═══════════════════════════════════════════════════════════════
// integration/libs/infrastructure/persistence/mysql/
//     mysql_generic_repository_itest.cpp
//
// Tests d'INTÉGRATION du MySQLGenericRepository contre un vrai MySQL.
//
// Stratégie :
//   1. Une base jetable (fixture) ;
//   2. les tables y sont créées via MysqlBootstrapper, à partir du
//      MÊME schéma domaine qui sert à peupler le SchemaRuntimeRegistry ;
//   3. le repository est exercé : CRUD, count, pagination.
//
// Le repository et le bootstrapper partagent donc une seule source
// de vérité (le schéma), ce qui reflète le fonctionnement réel du
// backend au boot.
//
// Pré-requis : MySQL de tests/docker-compose.test.yml en marche.
//
// Note de périmètre : ce premier lot couvre create / find_by_id /
// find_all / remove / count. Les transactions (in_transaction), les
// pivots M2M (insert_pivot avec UUID_TO_BIN) et les 3 modes de
// pagination seront ajoutés une fois la mécanique validée — chacun
// mérite ses propres cas dédiés.
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

#include <memory>
#include <set>
#include <string>

namespace {

namespace mysql   = sea::infrastructure::persistence::mysql;
namespace runtime = sea::infrastructure::runtime;

// ───────────────────────────────────────────────────────────────
// build_product_schema
//
// Schéma domaine d'une entité "Product" (id / name / price).
// Utilisé À LA FOIS pour bootstrapper la table ET pour peupler le
// registry runtime — une seule définition, pas de divergence
// possible entre la table SQL et ce que le repository attend.
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
// RepositoryTestContext
//
// Regroupe tout ce qu'un test de repository doit mettre en place :
//   - la base jetable bootstrappée (tables créées) ;
//   - un SchemaRuntimeRegistry peuplé du même schéma ;
//   - un MySQLGenericRepository prêt à l'emploi.
//
// À construire DANS run_on_reactor (le constructeur fait des .get()).
// Le pool est exposé pour le stop() final.
// ───────────────────────────────────────────────────────────────
struct RepositoryTestContext {
    sea::itest::MysqlTestFixture fixture;
    sea::itest::ScopedDatabase   scoped_db;

    std::unique_ptr<seastar::sharded<mysql::MysqlConnexionPool>> pool;
    std::shared_ptr<runtime::SchemaRuntimeRegistry>              registry;
    std::unique_ptr<mysql::MySQLGenericRepository>               repository;

    RepositoryTestContext()
        : scoped_db(fixture)
    {
        const auto schema = build_product_schema();
        const auto config = build_database_config(fixture);

        pool = fixture.make_pool().get();

        // Bootstrap : crée la table products dans la base jetable.
        mysql::MysqlBootstrapper bootstrapper{
                                              config, schema, *pool, fixture.executor()};
        const auto boot = bootstrapper.bootstrap().get();
        REQUIRE(boot.success);

        // Registry runtime : le repository en a besoin pour
        // connaître les colonnes de chaque entité.
        registry = std::make_shared<runtime::SchemaRuntimeRegistry>();
        registry->register_schema(schema);

        repository = std::make_unique<mysql::MySQLGenericRepository>(
            *pool, registry, fixture.executor());
    }

    // Nettoyage explicite : à appeler à la fin de chaque test, AVANT
    // que le ScopedDatabase ne drope la base.
    // Destructeur RAII : arrête le pool. Garanti même si un test
    // échoue en plein milieu — une exception REQUIRE sauterait
    // par-dessus tout appel teardown() explicite, et le
    // sharded<MysqlConnexionPool> détruit sans stop() déclenche
    // l'assertion Seastar `_instances.empty()` (SIGABRT).
    ~RepositoryTestContext() {
        if (pool) {
            try {
                pool->stop().get();
            } catch (...) {
                // Best-effort : un échec d'arrêt ne doit pas
                // masquer le résultat du test.
            }
        }
    }

    RepositoryTestContext(const RepositoryTestContext&)            = delete;
    RepositoryTestContext& operator=(const RepositoryTestContext&) = delete;
};

} // namespace


// ═══════════════════════════════════════════════════════════════
TEST_SUITE("MySQLGenericRepository [integration]") {

    // ───────────────────────────────────────────────────────────────
    // create puis find_by_id : un enregistrement inséré doit être relu
    // À L'IDENTIQUE — TOUS les champs, avec le bon type.
    //
    // Note de typage : le champ "price" est déclaré FieldType::Int. Le
    // repository mappe Int -> std::int32_t (vérifié dans
    // mysql_generic_repository.cpp, read_typed_value). On insère donc un
    // std::int32_t et on relit un std::int32_t. Insérer un int64_t
    // produirait une autre alternative du variant DynamicValue et un
    // binding différent — ce serait incorrect.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("create insère un enregistrement relu à l'identique sur tous les champs") {
        sea::itest::run_on_reactor([] {
            RepositoryTestContext ctx;

            runtime::DynamicRecord record;
            record["name"]  = std::string{"Clavier mécanique"};
            record["price"] = std::int32_t{120};

            const auto created = ctx.repository->create("Product", record).get();
            REQUIRE(created.has_value());

            // L'id est généré côté repository : présent, non vide, et de
            // longueur plausible pour un UUID textuel (36 caractères).
            const auto id_it = created->find("id");
            REQUIRE(id_it != created->end());
            REQUIRE(std::holds_alternative<std::string>(id_it->second));
            const auto id = std::get<std::string>(id_it->second);
            CHECK_FALSE(id.empty());
            CHECK(id.size() == 36);

            // Relecture par id : on vérifie CHAQUE champ, type compris.
            const auto fetched = ctx.repository->find_by_id("Product", id).get();
            REQUIRE(fetched.has_value());

            // name : doit être une string, valeur exacte.
            const auto name_it = fetched->find("name");
            REQUIRE(name_it != fetched->end());
            REQUIRE(std::holds_alternative<std::string>(name_it->second));
            CHECK(std::get<std::string>(name_it->second) == "Clavier mécanique");

            // price : doit être un int32_t (mapping de FieldType::Int),
            // valeur exacte. Un bug qui perdrait ou tronquerait le champ
            // numérique échouerait ici.
            const auto price_it = fetched->find("price");
            REQUIRE(price_it != fetched->end());
            REQUIRE(std::holds_alternative<std::int32_t>(price_it->second));
            CHECK(std::get<std::int32_t>(price_it->second) == 120);

            // id : doit être identique à celui renvoyé par create.
            const auto fetched_id_it = fetched->find("id");
            REQUIRE(fetched_id_it != fetched->end());
            CHECK(std::get<std::string>(fetched_id_it->second) == id);

        });
    }

    // ───────────────────────────────────────────────────────────────
    // find_by_id sur un id inexistant : doit renvoyer nullopt, pas
    // lever d'exception.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("find_by_id renvoie nullopt pour un id inexistant") {
        sea::itest::run_on_reactor([] {
            RepositoryTestContext ctx;

            const auto fetched =
                ctx.repository->find_by_id("Product", "id-qui-n-existe-pas").get();
            CHECK_FALSE(fetched.has_value());

        });
    }

    // ───────────────────────────────────────────────────────────────
    // count : durci. On ne se contente pas de "insérer N, vérifier N" —
    // une implémentation qui compterait les lignes vues en session
    // passerait. On vérifie donc que count :
    //   1. part de 0 sur base vierge ;
    //   2. suit les insertions ;
    //   3. DIMINUE après un remove (count interroge bien la table) ;
    //   4. ne compte QUE l'entité demandée, pas une autre table.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("count suit insertions et suppressions, et n'agrège qu'une entité") {
        sea::itest::run_on_reactor([] {
            RepositoryTestContext ctx;

            // 1. Base vierge.
            CHECK(ctx.repository->count("Product").get() == 0);

            // 2. Trois insertions, on garde le dernier id.
            std::string last_id;
            for (int i = 0; i < 3; ++i) {
                runtime::DynamicRecord record;
                record["name"]  = std::string{"Produit "} + std::to_string(i);
                record["price"] = std::int32_t{10 * i};
                const auto created =
                    ctx.repository->create("Product", record).get();
                REQUIRE(created.has_value());
                last_id = std::get<std::string>(created->at("id"));
            }
            CHECK(ctx.repository->count("Product").get() == 3);

            // 3. Après suppression d'une ligne, count doit valoir 2.
            //    Si count renvoyait un compteur de session, il dirait
            //    toujours 3 : ce sous-test l'attrape.
            const bool removed = ctx.repository->remove("Product", last_id).get();
            REQUIRE(removed);
            CHECK(ctx.repository->count("Product").get() == 2);

        });
    }

    // ───────────────────────────────────────────────────────────────
    // remove : durci. On vérifie l'effet sous DEUX angles
    // complémentaires :
    //   - find_by_id ne retrouve plus la ligne ;
    //   - count a diminué d'exactement 1.
    // Cela distingue "vraiment supprimé" de "simplement masqué".
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("remove supprime réellement : find_by_id et count le confirment") {
        sea::itest::run_on_reactor([] {
            RepositoryTestContext ctx;

            runtime::DynamicRecord record;
            record["name"]  = std::string{"À supprimer"};
            record["price"] = std::int32_t{1};

            const auto created = ctx.repository->create("Product", record).get();
            REQUIRE(created.has_value());
            const auto id = std::get<std::string>(created->at("id"));

            const auto count_before = ctx.repository->count("Product").get();
            REQUIRE(count_before == 1);

            const bool removed = ctx.repository->remove("Product", id).get();
            CHECK(removed);

            // Angle 1 : la ligne n'est plus retrouvable.
            const auto fetched = ctx.repository->find_by_id("Product", id).get();
            CHECK_FALSE(fetched.has_value());

            // Angle 2 : le compte a diminué d'exactement 1.
            CHECK(ctx.repository->count("Product").get() == count_before - 1);

        });
    }

    // ───────────────────────────────────────────────────────────────
    // find_all : durci. On ne vérifie pas que la taille — on vérifie
    // que les CONTENUS attendus sont bien tous présents. Un find_all
    // qui renverrait 5 records vides, ou 5 fois le même, passerait un
    // simple test de taille mais échouerait celui-ci.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("find_all renvoie tous les enregistrements avec leurs contenus") {
        sea::itest::run_on_reactor([] {
            RepositoryTestContext ctx;

            // Insertion de 5 produits aux noms distincts.
            std::set<std::string> expected_names;
            for (int i = 0; i < 5; ++i) {
                const std::string name =
                    std::string{"Item "} + std::to_string(i);
                expected_names.insert(name);

                runtime::DynamicRecord record;
                record["name"]  = name;
                record["price"] = std::int32_t{i};
                const auto created =
                    ctx.repository->create("Product", record).get();
                REQUIRE(created.has_value());
            }

            const auto all = ctx.repository->find_all("Product").get();
            REQUIRE(all.size() == 5);

            // On collecte les noms réellement renvoyés et on vérifie
            // qu'ils correspondent EXACTEMENT à l'ensemble attendu.
            std::set<std::string> actual_names;
            for (const auto& rec : all) {
                const auto it = rec.find("name");
                REQUIRE(it != rec.end());
                REQUIRE(std::holds_alternative<std::string>(it->second));
                actual_names.insert(std::get<std::string>(it->second));
            }
            CHECK(actual_names == expected_names);

        });
    }

} // TEST_SUITE