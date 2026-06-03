// ═══════════════════════════════════════════════════════════════
// integration/libs/infrastructure/persistence/mysql/
//     mysql_bootstrapper_itest.cpp
//
// Tests d'INTÉGRATION du MysqlBootstrapper contre un vrai MySQL.
//
// Ce que ces tests couvrent :
//   - ensure_database_exists  : CREATE DATABASE quand elle manque
//   - ensure_sea_files_table  : création idempotente de sea_files
//   - bootstrap (compute_and_apply_diff) : CREATE TABLE depuis un
//     schéma domaine, sur une base vierge
//
// Ce que ces tests vérifient en plus du "ça ne crashe pas" :
//   l'état RÉEL de MySQL après coup, relu via MysqlIntrospector.
//   C'est la différence avec un test unitaire : on observe l'effet
//   de bord persistant, pas seulement la valeur de retour.
//
// Pré-requis d'exécution :
//   Le MySQL de tests/docker-compose.test.yml doit tourner.
//   Sans lui, ces tests échouent à la connexion (attendu).
//
// Chaque TEST_CASE s'exécute dans run_on_reactor (pour Seastar) et
// utilise ScopedDatabase (création/destruction RAII de la base
// jetable). Aucun test ne touche une base partagée.
// ═══════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include "support/seastar_test_harness.h"
#include "support/mysql_test_fixture.h"

#include "persistence/mysql/mysql_bootstrapper.h"
#include "persistence/mysql/mysql_introspector.h"

#include "database_config.h"
#include "schema.h"
#include "entity.h"
#include "field.h"
#include "field_type.h"

#include <seastar/core/future.hh>

namespace {

namespace mysql = sea::infrastructure::persistence::mysql;

// ───────────────────────────────────────────────────────────────
// build_database_config
//
// Fabrique une DatabaseConfig MySQL pointant sur la base jetable de
// la fixture. Migrations activées en mode Conservative : le
// bootstrap ne fera que des CREATE TABLE / ADD COLUMN, jamais de
// DROP — c'est le mode sûr et celui qu'on veut éprouver en premier.
// ───────────────────────────────────────────────────────────────
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
    config.migrations.dry_run                    = false;

    return config;
}

// ───────────────────────────────────────────────────────────────
// build_minimal_schema
//
// Schéma domaine minimal : une seule entité "Product" avec un id et
// deux champs simples. Suffisant pour vérifier qu'un CREATE TABLE
// part bien du schéma et atterrit dans MySQL.
//
// Construit le domaine "à la main" volontairement : on teste le
// Bootstrapper, pas le YamlSchemaParser (déjà couvert en unitaire).
// ───────────────────────────────────────────────────────────────
sea::domain::Schema build_minimal_schema()
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

} // namespace


// ═══════════════════════════════════════════════════════════════
TEST_SUITE("MysqlBootstrapper [integration]") {

    // ───────────────────────────────────────────────────────────────
    // Smoke test de la chaîne complète : la fixture sait-elle créer et
    // détruire une base jetable, et s'y connecter via un pool ?
    //
    // Si CE test échoue, c'est l'infrastructure de test elle-même qui
    // est en cause (Docker pas lancé, GRANT manquant, port faux), pas
    // le code métier. Il sert de diagnostic de premier niveau.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("la fixture crée une base jetable et un pool fonctionnel") {
        sea::itest::run_on_reactor([] {
            sea::itest::MysqlTestFixture fixture;
            sea::itest::ScopedDatabase  db{fixture};   // CREATE / DROP RAII

            auto pool = fixture.make_pool().get();

            // Le pool est démarré ; on vérifie juste qu'on peut le
            // stopper proprement (le vrai test fonctionnel suit).
            CHECK(pool != nullptr);

            pool->stop().get();
        });
    }

    // ───────────────────────────────────────────────────────────────
    // DIAGNOSTIC — isole le strict minimum : construire un bootstrapper
    // et appeler ensure_database_exists(), SANS introspecteur ensuite.
    //
    // But : savoir si le SIGSEGV vient du bootstrapper lui-même ou de
    // l'introspecteur appelé juste après. Si ce test passe et que le
    // suivant crashe, le coupable est l'introspecteur. S'il crashe ici,
    // c'est le bootstrapper (ou la façon dont on l'appelle).
    //
    // config et schema sont déclarés AVANT le bootstrapper : leur durée
    // de vie englobe donc strictement la sienne (destruction en ordre
    // inverse), ce qui garantit que les références _config/_schema
    // qu'il stocke ne deviennent jamais pendantes.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("DIAG ensure_database_exists seul (sans introspecteur)") {
        sea::itest::run_on_reactor([] {
            sea::itest::MysqlTestFixture fixture;
            sea::itest::ScopedDatabase  db{fixture};

            // Ordre de déclaration = ordre de vie. config/schema d'abord,
            // bootstrapper ensuite : il sera détruit en premier.
            const sea::domain::DatabaseConfig config = build_database_config(fixture);
            const sea::domain::Schema        schema = build_minimal_schema();

            auto pool = fixture.make_pool().get();

            {
                mysql::MysqlBootstrapper bootstrapper{
                                                      config, schema, *pool, fixture.executor()};

                const bool ok = bootstrapper.ensure_database_exists().get();
                CHECK(ok);
            } // bootstrapper détruit ici, AVANT config/schema/pool

            pool->stop().get();
        });
    }

    // ───────────────────────────────────────────────────────────────
    // ensure_database_exists : sur une base jetable déjà créée par la
    // fixture, l'appel doit réussir sans erreur (idempotence) et la
    // base doit être visible par l'introspecteur.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("ensure_database_exists réussit et la base est visible") {
        sea::itest::run_on_reactor([] {
            sea::itest::MysqlTestFixture fixture;
            sea::itest::ScopedDatabase  db{fixture};

            auto pool = fixture.make_pool().get();

            const auto config = build_database_config(fixture);
            const auto schema = build_minimal_schema();

            mysql::MysqlBootstrapper bootstrapper{
                                                  config, schema, *pool, fixture.executor()};

            const bool ok = bootstrapper.ensure_database_exists().get();
            CHECK(ok);

            // Vérification de l'état réel : la base existe-t-elle ?
            mysql::MysqlIntrospector introspector{*pool, fixture.executor()};
            const bool exists =
                introspector.database_exists(fixture.database_name()).get();
            CHECK(exists);

            pool->stop().get();
        });
    }

    // ───────────────────────────────────────────────────────────────
    // ensure_sea_files_table : la table système sea_files doit être
    // créée. Appel répété => toujours OK (CREATE TABLE IF NOT EXISTS).
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("ensure_sea_files_table crée la table et est idempotent") {
        sea::itest::run_on_reactor([] {
            sea::itest::MysqlTestFixture fixture;
            sea::itest::ScopedDatabase  db{fixture};

            auto pool = fixture.make_pool().get();

            const auto config = build_database_config(fixture);
            const auto schema = build_minimal_schema();

            mysql::MysqlBootstrapper bootstrapper{
                                                  config, schema, *pool, fixture.executor()};

            // Premier appel : crée la table.
            CHECK(bootstrapper.ensure_sea_files_table().get());

            // Second appel : doit rester OK, sans erreur de table déjà
            // existante (c'est tout l'intérêt de l'idempotence).
            CHECK(bootstrapper.ensure_sea_files_table().get());

            // État réel : sea_files est bien présente.
            mysql::MysqlIntrospector introspector{*pool, fixture.executor()};
            const auto snapshot =
                introspector.snapshot(fixture.database_name()).get();
            CHECK(snapshot.has_table("sea_files"));

            pool->stop().get();
        });
    }

    // ───────────────────────────────────────────────────────────────
    // bootstrap complet : depuis un schéma domaine, sur base vierge,
    // la table "products" doit apparaître avec ses colonnes.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("bootstrap crée la table d'entité avec ses colonnes") {
        sea::itest::run_on_reactor([] {
            sea::itest::MysqlTestFixture fixture;
            sea::itest::ScopedDatabase  db{fixture};

            auto pool = fixture.make_pool().get();

            const auto config = build_database_config(fixture);
            const auto schema = build_minimal_schema();

            mysql::MysqlBootstrapper bootstrapper{
                                                  config, schema, *pool, fixture.executor()};

            const auto result = bootstrapper.bootstrap().get();

            REQUIRE(result.success);
            CHECK(result.errors.empty());

            // État réel : la table products et ses colonnes existent.
            mysql::MysqlIntrospector introspector{*pool, fixture.executor()};
            const auto snapshot =
                introspector.snapshot(fixture.database_name()).get();

            REQUIRE(snapshot.has_table("products"));

            const auto* table = snapshot.find_table("products");
            REQUIRE(table != nullptr);
            CHECK(table->has_column("id"));
            CHECK(table->has_column("name"));
            CHECK(table->has_column("price"));

            pool->stop().get();
        });
    }

} // TEST_SUITE