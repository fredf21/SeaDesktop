// ═══════════════════════════════════════════════════════════════
// integration/libs/infrastructure/persistence/mysql/
//     mysql_migration_itest.cpp
//
// Tests d'INTÉGRATION des migrations de schéma.
//
// Scénario commun à tous les tests :
//   1. bootstrap d'un schéma v1 → la table existe en version 1 ;
//   2. bootstrap d'un schéma v2 (modifié) avec un mode de migration
//      donné → le MysqlBootstrapper calcule le diff et l'applique ;
//   3. on relit l'état RÉEL via MysqlIntrospector pour vérifier ce
//      qui a été appliqué — et aussi ce qui a été IGNORÉ.
//
// ── Ce que ces tests éprouvent vraiment ────────────────────────
//
// Les migrations sont le domaine le plus dangereux : une migration
// ratée peut détruire des données. Le point central testé n'est pas
// seulement "le ALTER s'exécute", mais "le MODE est respecté" :
//   - Conservative : ADD COLUMN seulement, jamais MODIFY/RENAME/DROP.
//   - Modified     : applique les changements SÛRS (is_safe).
//   - Aggressive   : applique tout, y compris les renames heuristiques.
//
// La même modification v1→v2 doit donc être appliquée OU ignorée
// selon le mode. Un test qui ne vérifierait qu'Aggressive raterait
// l'essentiel de la logique.
//
// Pré-requis : MySQL de tests/docker-compose.test.yml en marche.
// ═══════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include "support/seastar_test_harness.h"
#include "support/mysql_test_fixture.h"

#include "persistence/mysql/mysql_bootstrapper.h"
#include "persistence/mysql/mysql_introspector.h"

#include "runtime/schema_runtime_registry.h"

#include "database_config.h"
#include "schema.h"
#include "entity.h"
#include "field.h"
#include "field_type.h"

#include <seastar/core/future.hh>

#include <memory>
#include <string>

namespace {

namespace mysql = sea::infrastructure::persistence::mysql;

// ───────────────────────────────────────────────────────────────
// Fabrique de Field — concis, pour composer des schémas variés.
// ───────────────────────────────────────────────────────────────
sea::domain::Field mk_field(const std::string& name,
                            sea::domain::FieldType type,
                            bool required = false)
{
    sea::domain::Field f;
    f.name     = name;
    f.type     = type;
    f.required = required;
    return f;
}

// build_config : config MySQL avec un mode de migration paramétrable.
sea::domain::DatabaseConfig
build_config(const sea::itest::MysqlTestFixture& fixture,
             sea::domain::MigrationMode mode)
{
    sea::domain::DatabaseConfig config;

    config.type          = sea::domain::DatabaseType::MySQL;
    config.host          = fixture.params().host;
    config.port          = static_cast<int>(fixture.params().port);
    config.database_name = fixture.database_name();
    config.username      = fixture.params().user;
    config.password      = fixture.params().password;

    config.migrations.enabled                    = true;
    config.migrations.mode                       = mode;
    config.migrations.create_database_if_missing = true;
    config.migrations.dry_run                    = false;

    return config;
}

// ───────────────────────────────────────────────────────────────
// MigrationTestContext
//
// Fournit une base jetable et un moyen d'appliquer un schéma
// arbitraire avec un mode donné, puis d'introspecter le résultat.
//
// apply_schema(schema, mode) construit un MysqlBootstrapper neuf
// (config + schema vivent dans la portée de l'appel mais le
// bootstrap est synchrone via .get(), donc les références restent
// valides le temps du bootstrap) et renvoie le BootstrapResult.
//
// snapshot() relit l'état réel de la base.
// ───────────────────────────────────────────────────────────────
struct MigrationTestContext {
    sea::itest::MysqlTestFixture fixture;
    sea::itest::ScopedDatabase   scoped_db;

    std::unique_ptr<seastar::sharded<mysql::MysqlConnexionPool>> pool;

    MigrationTestContext()
        : scoped_db(fixture)
    {
        pool = fixture.make_pool().get();
    }

    // Applique `schema` avec le mode `mode`. Synchrone (.get()).
    mysql::BootstrapResult apply_schema(const sea::domain::Schema& schema,
                                        sea::domain::MigrationMode mode) {
        const auto config = build_config(fixture, mode);
        mysql::MysqlBootstrapper bootstrapper{
                                              config, schema, *pool, fixture.executor()};
        return bootstrapper.bootstrap().get();
    }

    // État réel de la base.
    mysql::SchemaSnapshot snapshot() {
        mysql::MysqlIntrospector introspector{*pool, fixture.executor()};
        return introspector.snapshot(fixture.database_name()).get();
    }

    // Destructeur RAII : arrête le pool. Garanti même si un test
    // échoue en plein milieu — une exception REQUIRE sauterait
    // par-dessus tout appel teardown() explicite, et le
    // sharded<MysqlConnexionPool> détruit sans stop() déclenche
    // l'assertion Seastar `_instances.empty()` (SIGABRT).
    ~MigrationTestContext() {
        if (pool) {
            try {
                pool->stop().get();
            } catch (...) {
                // Best-effort : un échec d'arrêt ne doit pas
                // masquer le résultat du test.
            }
        }
    }

    MigrationTestContext(const MigrationTestContext&)            = delete;
    MigrationTestContext& operator=(const MigrationTestContext&) = delete;
};

// ───────────────────────────────────────────────────────────────
// Helpers de construction de schémas v1/v2 pour l'entité "Account".
// ───────────────────────────────────────────────────────────────

// v1 : Account(id, email). Schéma de départ commun.
sea::domain::Schema account_v1()
{
    using sea::domain::FieldType;

    sea::domain::Entity account;
    account.name       = "Account";
    account.table_name = "accounts";
    account.fields     = {
        mk_field("id",    FieldType::UUID,   true),
        mk_field("email", FieldType::String, true),
    };

    sea::domain::Schema schema;
    schema.entities = {account};
    return schema;
}

} // namespace


// ═══════════════════════════════════════════════════════════════
TEST_SUITE("Migrations : ADD COLUMN [integration]") {

    // ───────────────────────────────────────────────────────────────
    // ADD COLUMN est l'opération SÛRE par excellence : elle est
    // appliquée dans TOUS les modes, Conservative compris.
    // v1 = Account(id, email) ; v2 ajoute "display_name".
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("ADD COLUMN est appliqué même en mode Conservative") {
        sea::itest::run_on_reactor([] {
            MigrationTestContext ctx;

            // v1 : table de départ.
            REQUIRE(ctx.apply_schema(account_v1(),
                                     sea::domain::MigrationMode::Conservative).success);

            // v2 : ajoute une colonne.
            auto v2 = account_v1();
            v2.entities[0].fields.push_back(
                mk_field("display_name", sea::domain::FieldType::String));

            const auto result = ctx.apply_schema(
                v2, sea::domain::MigrationMode::Conservative);

            REQUIRE(result.success);
            // Le résultat doit lister la colonne ajoutée.
            CHECK_FALSE(result.columns_added.empty());

            // État réel : la colonne existe vraiment.
            const auto snap = ctx.snapshot();
            const auto* table = snap.find_table("accounts");
            REQUIRE(table != nullptr);
            CHECK(table->has_column("display_name"));

        });
    }

} // TEST_SUITE


// ═══════════════════════════════════════════════════════════════
TEST_SUITE("Migrations : modes de migration [integration]") {

    // ───────────────────────────────────────────────────────────────
    // Le cœur du sujet : le MODE gouverne ce qui est appliqué.
    //
    // On prend une modification de type MODIFY COLUMN (changement de
    // type) et on l'applique sous les trois modes, sur trois bases
    // jetables distinctes. Attendu :
    //   - Conservative : ignorée → warning, colonne inchangée.
    //   - Aggressive   : appliquée → colonne modifiée.
    //
    // On n'affirme rien de rigide sur Modified ici (selon que le
    // changement est jugé safe ou non par l'heuristique du differ) —
    // ce mode est couvert spécifiquement par les tests RENAME ci-après,
    // où la frontière safe/unsafe est nette.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("MODIFY COLUMN : Conservative ignore, Aggressive applique") {
        sea::itest::run_on_reactor([] {
            // ── Sous-cas Conservative ───────────────────────────────
            {
                MigrationTestContext ctx;
                REQUIRE(ctx.apply_schema(
                               account_v1(),
                               sea::domain::MigrationMode::Conservative).success);

                // v2 : email passe de String à Text (changement de type).
                auto v2 = account_v1();
                v2.entities[0].fields[1] =
                    mk_field("email", sea::domain::FieldType::Text, true);

                const auto result = ctx.apply_schema(
                    v2, sea::domain::MigrationMode::Conservative);

                REQUIRE(result.success);
                // Conservative : le changement de type est ignoré → il
                // doit apparaître dans les warnings, pas dans
                // columns_modified.
                CHECK(result.columns_modified.empty());
                CHECK_FALSE(result.warnings.empty());

            }

            // ── Sous-cas Aggressive ─────────────────────────────────
            {
                MigrationTestContext ctx;
                REQUIRE(ctx.apply_schema(
                               account_v1(),
                               sea::domain::MigrationMode::Aggressive).success);

                auto v2 = account_v1();
                v2.entities[0].fields[1] =
                    mk_field("email", sea::domain::FieldType::Text, true);

                const auto result = ctx.apply_schema(
                    v2, sea::domain::MigrationMode::Aggressive);

                REQUIRE(result.success);
                // Aggressive : le changement de type est appliqué.
                CHECK_FALSE(result.columns_modified.empty());

            }
        });
    }

} // TEST_SUITE


// ═══════════════════════════════════════════════════════════════
TEST_SUITE("Migrations : RENAME COLUMN [integration]") {

    // ───────────────────────────────────────────────────────────────
    // RENAME explicite (annotation previous_name, score = 100).
    //
    // v1 : Account(id, email).
    // v2 : Account(id, email_address) où email_address porte
    //      previous_name = "email".
    //
    // Un rename explicite est considéré SÛR : il doit être appliqué en
    // mode Modified (et Aggressive). On vérifie en mode Modified.
    //
    // État attendu après migration : la colonne "email" n'existe plus,
    // "email_address" existe.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("RENAME explicite (previous_name) est appliqué en mode Modified") {
        sea::itest::run_on_reactor([] {
            MigrationTestContext ctx;

            REQUIRE(ctx.apply_schema(
                           account_v1(),
                           sea::domain::MigrationMode::Modified).success);

            // v2 : email → email_address, avec annotation explicite.
            auto v2 = account_v1();
            auto renamed = mk_field("email_address",
                                    sea::domain::FieldType::String, true);
            renamed.previous_name = "email";
            v2.entities[0].fields[1] = renamed;

            const auto result = ctx.apply_schema(
                v2, sea::domain::MigrationMode::Modified);

            REQUIRE(result.success);
            // Le rename doit être listé.
            CHECK_FALSE(result.columns_renamed.empty());

            // État réel : ancien nom parti, nouveau nom présent.
            const auto snap = ctx.snapshot();
            const auto* table = snap.find_table("accounts");
            REQUIRE(table != nullptr);
            CHECK_FALSE(table->has_column("email"));
            CHECK(table->has_column("email_address"));

        });
    }

    // ───────────────────────────────────────────────────────────────
    // RENAME explicite IGNORÉ en mode Conservative.
    //
    // Même modification que ci-dessus, mais en mode Conservative : le
    // rename ne doit PAS être appliqué. La colonne "email" doit rester,
    // et "email_address" ne doit pas exister.
    //
    // C'est le pendant négatif du test précédent : il prouve que le
    // mode est réellement déterminant, pas décoratif.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("RENAME explicite est IGNORÉ en mode Conservative") {
        sea::itest::run_on_reactor([] {
            MigrationTestContext ctx;

            REQUIRE(ctx.apply_schema(
                           account_v1(),
                           sea::domain::MigrationMode::Conservative).success);

            auto v2 = account_v1();
            auto renamed = mk_field("email_address",
                                    sea::domain::FieldType::String, true);
            renamed.previous_name = "email";
            v2.entities[0].fields[1] = renamed;

            const auto result = ctx.apply_schema(
                v2, sea::domain::MigrationMode::Conservative);

            REQUIRE(result.success);
            // Conservative : pas de rename, mais un warning.
            CHECK(result.columns_renamed.empty());
            CHECK_FALSE(result.warnings.empty());

            // État réel : "email" toujours là, "email_address" absent.
            const auto snap = ctx.snapshot();
            const auto* table = snap.find_table("accounts");
            REQUIRE(table != nullptr);
            CHECK(table->has_column("email"));
            CHECK_FALSE(table->has_column("email_address"));

        });
    }

} // TEST_SUITE


// ═══════════════════════════════════════════════════════════════
TEST_SUITE("Migrations : index et unique [integration]") {

    // ───────────────────────────────────────────────────────────────
    // ADD INDEX : v2 marque le champ "email" comme indexed. En mode
    // Aggressive, l'index doit être créé sur la table.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("ADD INDEX crée un index sur la colonne en mode Aggressive") {
        sea::itest::run_on_reactor([] {
            MigrationTestContext ctx;

            REQUIRE(ctx.apply_schema(
                           account_v1(),
                           sea::domain::MigrationMode::Aggressive).success);

            // v2 : email devient indexed.
            auto v2 = account_v1();
            v2.entities[0].fields[1].indexed = true;

            const auto result = ctx.apply_schema(
                v2, sea::domain::MigrationMode::Aggressive);

            REQUIRE(result.success);
            CHECK_FALSE(result.indexes_changed.empty());

            // État réel : un index couvrant la colonne email existe.
            const auto snap = ctx.snapshot();
            const auto* table = snap.find_table("accounts");
            REQUIRE(table != nullptr);

            bool email_indexed = false;
            for (const auto& idx : table->indexes) {
                for (const auto& col : idx.columns) {
                    if (col == "email") {
                        email_indexed = true;
                    }
                }
            }
            CHECK(email_indexed);

        });
    }

    // ───────────────────────────────────────────────────────────────
    // ADD UNIQUE : v2 marque "email" comme unique. En mode Aggressive,
    // une contrainte d'unicité doit être créée.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("ADD UNIQUE crée une contrainte d'unicité en mode Aggressive") {
        sea::itest::run_on_reactor([] {
            MigrationTestContext ctx;

            REQUIRE(ctx.apply_schema(
                           account_v1(),
                           sea::domain::MigrationMode::Aggressive).success);

            auto v2 = account_v1();
            v2.entities[0].fields[1].unique = true;

            const auto result = ctx.apply_schema(
                v2, sea::domain::MigrationMode::Aggressive);

            REQUIRE(result.success);
            CHECK_FALSE(result.indexes_changed.empty());

            // État réel : un index UNIQUE couvre la colonne email.
            const auto snap = ctx.snapshot();
            const auto* table = snap.find_table("accounts");
            REQUIRE(table != nullptr);

            bool email_unique = false;
            for (const auto& idx : table->indexes) {
                if (!idx.is_unique) {
                    continue;
                }
                for (const auto& col : idx.columns) {
                    if (col == "email") {
                        email_unique = true;
                    }
                }
            }
            CHECK(email_unique);

        });
    }

} // TEST_SUITE


// ═══════════════════════════════════════════════════════════════
TEST_SUITE("Migrations : idempotence [integration]") {

    // ───────────────────────────────────────────────────────────────
    // Idempotence : ré-appliquer le MÊME schéma ne doit produire aucun
    // changement. C'est essentiel — le bootstrap tourne à chaque
    // démarrage du serveur ; un second boot sur un schéma inchangé ne
    // doit ni ajouter, ni modifier, ni renommer quoi que ce soit.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("ré-appliquer le même schéma ne produit aucun changement") {
        sea::itest::run_on_reactor([] {
            MigrationTestContext ctx;

            // Premier bootstrap : crée la table.
            const auto first = ctx.apply_schema(
                account_v1(), sea::domain::MigrationMode::Aggressive);
            REQUIRE(first.success);

            // Second bootstrap, schéma identique : diff vide attendu.
            const auto second = ctx.apply_schema(
                account_v1(), sea::domain::MigrationMode::Aggressive);

            REQUIRE(second.success);
            CHECK(second.columns_added.empty());
            CHECK(second.columns_modified.empty());
            CHECK(second.columns_renamed.empty());
            CHECK(second.indexes_changed.empty());
            // La table existait déjà : elle ne doit pas être recréée.
            CHECK(second.tables_created.empty());

        });
    }

} // TEST_SUITE