// ═══════════════════════════════════════════════════════════════
// integration/libs/infrastructure/persistence/mysql/
//     mysql_pivot_itest.cpp
//
// Tests d'INTÉGRATION des opérations sur table pivot many-to-many :
// insert_pivot, delete_pivot, pivot_exists.
//
// ── L'enjeu central : le wrapping UUID_TO_BIN ──────────────────
//
// Une table pivot M2M a ses deux colonnes en BINARY(16) (cf.
// MysqlSchemaGenerator::generate_pivot_table_sql). Or les id des
// entités, renvoyés par create(), sont des UUID TEXTUELS de 36
// caractères. Pour qu'un INSERT/DELETE/SELECT sur le pivot
// fonctionne, le repository doit convertir : il enveloppe chaque
// valeur détectée comme UUID dans UUID_TO_BIN(?, 1).
//
// Cette détection est heuristique (mysql_generic_repository.cpp) :
// une string de 36 caractères avec des '-' aux positions 8/13/18/23
// est traitée comme un UUID. Si le wrapping disparaissait ou si
// l'heuristique se cassait, l'INSERT échouerait (type BINARY vs
// string) et insert_pivot renverrait false → ces tests le
// détecteraient immédiatement.
//
// ── Contrainte de clé étrangère ────────────────────────────────
//
// Le pivot référence User.id et Role.id par des FK. On ne peut donc
// PAS insérer une association vers des id inexistants : il faut
// d'abord créer de vrais User et Role. Les tests le font, et un cas
// dédié vérifie justement qu'une association orpheline est rejetée.
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
#include "relation.h"

#include <seastar/core/future.hh>

#include <memory>
#include <string>

namespace {

namespace mysql   = sea::infrastructure::persistence::mysql;
namespace runtime = sea::infrastructure::runtime;

// Nom de la table pivot, partagé par les tests.
constexpr const char* kPivotTable = "user_roles";

// ───────────────────────────────────────────────────────────────
// build_m2m_schema
//
// Deux entités User et Role, plus une relation ManyToMany portée
// par User. Le bootstrapper en déduit :
//   - table users(id, name)
//   - table roles(id, label)
//   - table pivot user_roles(user_id BINARY(16), role_id BINARY(16))
//     avec FK vers users.id et roles.id.
// ───────────────────────────────────────────────────────────────
sea::domain::Schema build_m2m_schema()
{
    using sea::domain::Entity;
    using sea::domain::Field;
    using sea::domain::FieldType;
    using sea::domain::Relation;
    using sea::domain::RelationKind;

    // ── Entité User ────────────────────────────────────────────
    Field user_id;
    user_id.name = "id";
    user_id.type = FieldType::UUID;
    user_id.required = true;

    Field user_name;
    user_name.name = "name";
    user_name.type = FieldType::String;
    user_name.required = true;

    // Relation M2M User <-> Role, via le pivot user_roles.
    Relation roles_relation;
    roles_relation.name             = "roles";
    roles_relation.target_entity    = "Role";
    roles_relation.kind             = RelationKind::ManyToMany;
    roles_relation.pivot_table      = kPivotTable;
    roles_relation.source_fk_column = "user_id";
    roles_relation.target_fk_column = "role_id";

    Entity user;
    user.name       = "User";
    user.table_name = "users";
    user.fields     = {user_id, user_name};
    user.relations  = {roles_relation};

    // ── Entité Role ────────────────────────────────────────────
    Field role_id;
    role_id.name = "id";
    role_id.type = FieldType::UUID;
    role_id.required = true;

    Field role_label;
    role_label.name = "label";
    role_label.type = FieldType::String;
    role_label.required = true;

    Entity role;
    role.name       = "Role";
    role.table_name = "roles";
    role.fields     = {role_id, role_label};

    sea::domain::Schema schema;
    schema.entities = {user, role};
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
// PivotTestContext
//
// Base jetable bootstrappée (users + roles + user_roles) + registry
// + repository. À construire DANS run_on_reactor.
//
// Expose create_user / create_role : créent une vraie ligne et
// renvoient l'UUID textuel généré — celui qu'on passera ensuite aux
// opérations pivot.
// ───────────────────────────────────────────────────────────────
struct PivotTestContext {
    sea::itest::MysqlTestFixture fixture;
    sea::itest::ScopedDatabase   scoped_db;

    std::unique_ptr<seastar::sharded<mysql::MysqlConnexionPool>> pool;
    std::shared_ptr<runtime::SchemaRuntimeRegistry>             registry;
    std::unique_ptr<mysql::MySQLGenericRepository>              repository;

    PivotTestContext()
        : scoped_db(fixture)
    {
        const auto schema = build_m2m_schema();
        const auto config = build_database_config(fixture);

        pool = fixture.make_pool().get();

        mysql::MysqlBootstrapper bootstrapper{
                                              config, schema, *pool, fixture.executor()};
        const auto boot = bootstrapper.bootstrap().get();
        REQUIRE(boot.success);
        // Le pivot doit avoir été créé par le bootstrap.
        REQUIRE_FALSE(boot.pivots_created.empty());

        registry = std::make_shared<runtime::SchemaRuntimeRegistry>();
        registry->register_schema(schema);

        repository = std::make_unique<mysql::MySQLGenericRepository>(
            *pool, registry, fixture.executor());
    }

    // Crée un User et renvoie son id (UUID textuel, 36 caractères).
    std::string create_user(const std::string& name) {
        runtime::DynamicRecord rec;
        rec["name"] = name;
        const auto created = repository->create("User", rec).get();
        REQUIRE(created.has_value());
        return std::get<std::string>(created->at("id"));
    }

    // Crée un Role et renvoie son id.
    std::string create_role(const std::string& label) {
        runtime::DynamicRecord rec;
        rec["label"] = label;
        const auto created = repository->create("Role", rec).get();
        REQUIRE(created.has_value());
        return std::get<std::string>(created->at("id"));
    }

    // Destructeur RAII : arrête le pool. Garanti même si un test
    // échoue en plein milieu — une exception REQUIRE sauterait
    // par-dessus tout appel teardown() explicite, et le
    // sharded<MysqlConnexionPool> détruit sans stop() déclenche
    // l'assertion Seastar `_instances.empty()` (SIGABRT).
    ~PivotTestContext() {
        if (pool) {
            try {
                pool->stop().get();
            } catch (...) {
                // Best-effort : un échec d'arrêt ne doit pas
                // masquer le résultat du test.
            }
        }
    }

    PivotTestContext(const PivotTestContext&)            = delete;
    PivotTestContext& operator=(const PivotTestContext&) = delete;
};

// Construit le DynamicRecord d'une association {user_id, role_id}.
// Les deux valeurs sont des UUID textuels : le repository les
// détectera et appliquera UUID_TO_BIN.
runtime::DynamicRecord make_link(const std::string& user_id,
                                 const std::string& role_id)
{
    runtime::DynamicRecord rec;
    rec["user_id"] = user_id;
    rec["role_id"] = role_id;
    return rec;
}

} // namespace


// ═══════════════════════════════════════════════════════════════
TEST_SUITE("MySQLGenericRepository pivots M2M [integration]") {

    // ───────────────────────────────────────────────────────────────
    // insert_pivot : insérer une association entre un User et un Role
    // existants doit réussir. C'est le test du wrapping UUID_TO_BIN :
    // si la conversion UUID textuel -> BINARY(16) ne se faisait pas,
    // l'INSERT échouerait et insert_pivot renverrait false.
    //
    // On confirme le succès sous deux angles : la valeur de retour ET
    // pivot_exists qui relit l'association.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("insert_pivot crée une association entre entités existantes") {
        sea::itest::run_on_reactor([] {
            PivotTestContext ctx;

            const auto user_id = ctx.create_user("Alice");
            const auto role_id = ctx.create_role("admin");

            const bool inserted = ctx.repository->insert_pivot(
                                                    kPivotTable, make_link(user_id, role_id)).get();
            CHECK(inserted);

            // L'association doit maintenant être visible.
            const bool exists = ctx.repository->pivot_exists(
                                                  kPivotTable, make_link(user_id, role_id)).get();
            CHECK(exists);

        });
    }

    // ───────────────────────────────────────────────────────────────
    // pivot_exists sur une association absente : doit renvoyer false,
    // sans erreur. On crée les entités mais on n'insère PAS le lien.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("pivot_exists renvoie false pour une association absente") {
        sea::itest::run_on_reactor([] {
            PivotTestContext ctx;

            const auto user_id = ctx.create_user("Bob");
            const auto role_id = ctx.create_role("viewer");

            // Aucune insertion : l'association ne doit pas exister.
            const bool exists = ctx.repository->pivot_exists(
                                                  kPivotTable, make_link(user_id, role_id)).get();
            CHECK_FALSE(exists);

        });
    }

    // ───────────────────────────────────────────────────────────────
    // delete_pivot : supprimer une association existante doit réussir
    // (renvoyer true) et l'association ne doit plus exister après.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("delete_pivot supprime une association existante") {
        sea::itest::run_on_reactor([] {
            PivotTestContext ctx;

            const auto user_id = ctx.create_user("Carla");
            const auto role_id = ctx.create_role("editor");

            REQUIRE(ctx.repository->insert_pivot(
                                      kPivotTable, make_link(user_id, role_id)).get());
            REQUIRE(ctx.repository->pivot_exists(
                                      kPivotTable, make_link(user_id, role_id)).get());

            // Suppression : doit renvoyer true (une ligne affectée).
            const bool removed = ctx.repository->delete_pivot(
                                                   kPivotTable, make_link(user_id, role_id)).get();
            CHECK(removed);

            // L'association ne doit plus exister.
            const bool exists = ctx.repository->pivot_exists(
                                                  kPivotTable, make_link(user_id, role_id)).get();
            CHECK_FALSE(exists);

        });
    }

    // ───────────────────────────────────────────────────────────────
    // delete_pivot sur une association inexistante : aucune ligne
    // affectée → doit renvoyer false (et non lever).
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("delete_pivot renvoie false si l'association n'existe pas") {
        sea::itest::run_on_reactor([] {
            PivotTestContext ctx;

            const auto user_id = ctx.create_user("Dan");
            const auto role_id = ctx.create_role("guest");

            // Jamais insérée : le delete ne doit affecter aucune ligne.
            const bool removed = ctx.repository->delete_pivot(
                                                   kPivotTable, make_link(user_id, role_id)).get();
            CHECK_FALSE(removed);

        });
    }

    // ───────────────────────────────────────────────────────────────
    // Un même User peut être associé à plusieurs Roles. On vérifie que
    // les associations sont indépendantes : insérer (user, role2) ne
    // touche pas (user, role1).
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("un User peut porter plusieurs associations indépendantes") {
        sea::itest::run_on_reactor([] {
            PivotTestContext ctx;

            const auto user_id = ctx.create_user("Eve");
            const auto role_a  = ctx.create_role("role_a");
            const auto role_b  = ctx.create_role("role_b");

            REQUIRE(ctx.repository->insert_pivot(
                                      kPivotTable, make_link(user_id, role_a)).get());
            REQUIRE(ctx.repository->insert_pivot(
                                      kPivotTable, make_link(user_id, role_b)).get());

            // Les deux associations coexistent.
            CHECK(ctx.repository->pivot_exists(
                                    kPivotTable, make_link(user_id, role_a)).get());
            CHECK(ctx.repository->pivot_exists(
                                    kPivotTable, make_link(user_id, role_b)).get());

            // Supprimer l'une ne supprime pas l'autre.
            REQUIRE(ctx.repository->delete_pivot(
                                      kPivotTable, make_link(user_id, role_a)).get());

            CHECK_FALSE(ctx.repository->pivot_exists(
                                          kPivotTable, make_link(user_id, role_a)).get());
            CHECK(ctx.repository->pivot_exists(
                                    kPivotTable, make_link(user_id, role_b)).get());

        });
    }

    // ───────────────────────────────────────────────────────────────
    // Cas d'erreur : insérer une association vers des id INEXISTANTS.
    // La table pivot a des FK vers users.id et roles.id ; un INSERT
    // référençant des UUID orphelins viole la contrainte FK. Le code
    // attrape la SQLException et renvoie false.
    //
    // Ce test vérifie aussi, indirectement, que le wrapping UUID_TO_BIN
    // fonctionne : les UUID utilisés ici sont bien formés (36 chars),
    // donc l'échec vient de la FK, PAS d'une erreur de type. Si le
    // wrapping était cassé, on aurait une autre erreur SQL — le
    // résultat false serait le même, mais le log dirait autre chose.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("insert_pivot échoue proprement pour des id inexistants") {
        sea::itest::run_on_reactor([] {
            PivotTestContext ctx;

            // UUID syntaxiquement valides mais qui ne correspondent à
            // aucune ligne users/roles.
            const std::string orphan_user = "00000000-0000-0000-0000-000000000001";
            const std::string orphan_role = "00000000-0000-0000-0000-000000000002";

            // L'association doit être refusée (violation de FK), mais
            // proprement : renvoie false, ne lève pas.
            bool inserted = true;
            REQUIRE_NOTHROW(
                inserted = ctx.repository->insert_pivot(
                                             kPivotTable, make_link(orphan_user, orphan_role)).get()
                );
            CHECK_FALSE(inserted);

            // Et l'association ne doit pas exister.
            const bool exists = ctx.repository->pivot_exists(
                                                  kPivotTable, make_link(orphan_user, orphan_role)).get();
            CHECK_FALSE(exists);

        });
    }

    // ───────────────────────────────────────────────────────────────
    // insert_pivot avec un record vide : le contrat du code de prod
    // stipule qu'un appel sans valeur lève une std::runtime_error
    // ("no value provided"). On vérifie ce contrat.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("insert_pivot lève si aucune valeur n'est fournie") {
        sea::itest::run_on_reactor([] {
            PivotTestContext ctx;

            runtime::DynamicRecord empty;
            CHECK_THROWS_AS(
                ctx.repository->insert_pivot(kPivotTable, empty).get(),
                std::runtime_error);

        });
    }

} // TEST_SUITE