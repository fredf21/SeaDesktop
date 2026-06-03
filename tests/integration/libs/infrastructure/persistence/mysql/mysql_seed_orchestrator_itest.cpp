// ═══════════════════════════════════════════════════════════════
// integration/libs/infrastructure/persistence/mysql/
//     mysql_seed_orchestrator_itest.cpp
//
// Tests d'INTÉGRATION du SeedOrchestrator.
//
// Le SeedOrchestrator insère, au boot, les données déclarées dans
// les `seeds` du schéma. Il fait trois choses non triviales que ces
// tests éprouvent une par une, en relisant la base ensuite :
//
//   1. ${REF:alias}   — résolution d'une référence : un seed peut
//      référencer un autre seed par son alias ; l'orchestrateur
//      remplace ${REF:alias} par l'UUID réellement attribué à ce
//      seed lors de son insertion. C'est ce qui permet de seeder
//      des clés étrangères.
//
//   2. {{hash:value}} — un champ Password contenant {{hash:secret}}
//      doit être stocké haché (bcrypt), jamais en clair.
//
//   3. pivots M2M     — les m2m_relations d'un SeedRecord génèrent
//      des lignes dans la table pivot, en résolvant les aliases
//      cibles vers leurs UUID.
//
// On teste aussi le MODE :
//   - Once   : ne seede que si la table est vide.
//   - Always : seede à chaque appel.
//
// Pré-requis : MySQL de tests/docker-compose.test.yml en marche.
// ═══════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include "persistence/mysql/mysql_generic_repository.h"
#include "support/seastar_test_harness.h"
#include "support/mysql_test_fixture.h"

#include "persistence/mysql/mysql_bootstrapper.h"
#include "persistence/mysql/mysql_introspector.h"
#include "persistence/mysql/seed_orchestrator.h"

#include "runtime/schema_runtime_registry.h"
#include "runtime/generic_validator.h"
#include "runtime/generic_crud_engine.h"
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

// Fabrique de Field concise (nommée mk_fld pour ne heurter aucun
// helper existant du domaine).
sea::domain::Field mk_fld(const std::string& name,
                          sea::domain::FieldType type,
                          bool required = false)
{
    sea::domain::Field f;
    f.name     = name;
    f.type     = type;
    f.required = required;
    return f;
}

sea::domain::DatabaseConfig
build_config(const sea::itest::MysqlTestFixture& fixture,
             sea::domain::SeedsMode seeds_mode,
             bool seeds_enabled = true)
{
    sea::domain::DatabaseConfig config;

    config.type          = sea::domain::DatabaseType::MySQL;
    config.host          = fixture.params().host;
    config.port          = static_cast<int>(fixture.params().port);
    config.database_name = fixture.database_name();
    config.username      = fixture.params().user;
    config.password      = fixture.params().password;

    config.migrations.enabled                    = true;
    config.migrations.mode                       = sea::domain::MigrationMode::Aggressive;
    config.migrations.create_database_if_missing = true;

    config.migrations.seeds.enabled = seeds_enabled;
    config.migrations.seeds.mode    = seeds_mode;

    return config;
}

// ───────────────────────────────────────────────────────────────
// SeedTestContext
//
// Assemble toute la chaîne dont dépend le SeedOrchestrator :
//   registry → validator → crud_engine, + introspector + repository.
//
// run_seed(schema, mode) bootstrappe le schéma, construit la chaîne,
// lance seed_all(), renvoie le SeedResult. Le repository reste
// accessible pour relire la base et vérifier l'effet réel.
// ───────────────────────────────────────────────────────────────
struct SeedTestContext {
    sea::itest::MysqlTestFixture fixture;
    sea::itest::ScopedDatabase   scoped_db;

    std::unique_ptr<seastar::sharded<mysql::MysqlConnexionPool>> pool;
    std::shared_ptr<runtime::SchemaRuntimeRegistry>             registry;
    std::shared_ptr<mysql::MySQLGenericRepository>              repository;

    SeedTestContext()
        : scoped_db(fixture)
    {
        pool = fixture.make_pool().get();
    }

    // Bootstrappe `schema`, exécute le seeding avec le mode `mode`,
    // renvoie le SeedResult.
    mysql::SeedResult run_seed(const sea::domain::Schema& schema,
                               sea::domain::SeedsMode mode,
                               bool seeds_enabled = true) {
        const auto config = build_config(fixture, mode, seeds_enabled);

        // 1. Bootstrap : crée les tables (et pivots) de ce schéma.
        mysql::MysqlBootstrapper bootstrapper{
                                              config, schema, *pool, fixture.executor()};
        REQUIRE(bootstrapper.bootstrap().get().success);

        // 2. Chaîne runtime : registry → validator → crud engine.
        registry = std::make_shared<runtime::SchemaRuntimeRegistry>();
        registry->register_schema(schema);

        repository = std::make_shared<mysql::MySQLGenericRepository>(
            *pool, registry, fixture.executor());

        auto validator = std::make_shared<runtime::GenericValidator>();
        auto crud_engine = std::make_shared<runtime::GenericCrudEngine>(
            registry, validator, repository);

        auto introspector = std::make_shared<mysql::MysqlIntrospector>(
            *pool, fixture.executor());

        // 3. SeedOrchestrator + exécution.
        mysql::SeedOrchestrator orchestrator{
                                             config, schema, crud_engine, introspector,
                                             fixture.executor(), repository};

        // seed_all() renvoie une future ; on est dans un seastar::thread
        // (run_on_reactor) donc .get() est légal.
        return orchestrator.seed_all().get();
    }

    // Compte les lignes d'une entité (via le repository déjà construit).
    std::size_t count(const std::string& entity_name) {
        return repository->count(entity_name).get();
    }

    // Destructeur RAII : arrête le pool. Garanti même si un test
    // échoue en plein milieu — une exception REQUIRE sauterait
    // par-dessus tout appel teardown() explicite, et le
    // sharded<MysqlConnexionPool> détruit sans stop() déclenche
    // l'assertion Seastar `_instances.empty()` (SIGABRT).
    ~SeedTestContext() {
        if (pool) {
            try {
                pool->stop().get();
            } catch (...) {
                // Best-effort : un échec d'arrêt ne doit pas
                // masquer le résultat du test.
            }
        }
    }

    SeedTestContext(const SeedTestContext&)            = delete;
    SeedTestContext& operator=(const SeedTestContext&) = delete;
};

// Helper : extrait un champ string d'un record.
std::string str_field(const runtime::DynamicRecord& rec,
                      const std::string& field)
{
    const auto it = rec.find(field);
    REQUIRE(it != rec.end());
    REQUIRE(std::holds_alternative<std::string>(it->second));
    return std::get<std::string>(it->second);
}

} // namespace


// ═══════════════════════════════════════════════════════════════
TEST_SUITE("SeedOrchestrator : seeding de base [integration]") {

    // ───────────────────────────────────────────────────────────────
    // Seeding simple : une entité Role avec deux SeedRecord. Après
    // seed_all, la table doit contenir exactement deux lignes.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("seed_all insère les SeedRecord déclarés dans le schéma") {
        sea::itest::run_on_reactor([] {
            SeedTestContext ctx;

            sea::domain::Entity role;
            role.name       = "Role";
            role.table_name = "roles";
            role.fields     = {
                mk_fld("id",    sea::domain::FieldType::UUID,   true),
                mk_fld("label", sea::domain::FieldType::String, true),
            };

            sea::domain::SeedRecord admin;
            admin.values["label"] = std::string{"admin"};
            sea::domain::SeedRecord viewer;
            viewer.values["label"] = std::string{"viewer"};
            role.seeds = {admin, viewer};

            sea::domain::Schema schema;
            schema.entities = {role};

            const auto result = ctx.run_seed(schema, sea::domain::SeedsMode::Once);

            REQUIRE(result.success);
            CHECK(result.errors.empty());

            // Effet réel : deux Role en base.
            CHECK(ctx.count("Role") == 2);

        });
    }

    // ───────────────────────────────────────────────────────────────
    // seeds.enabled = false : seed_all ne doit RIEN insérer, même si le
    // schéma contient des SeedRecord.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("seed_all n'insère rien quand seeds.enabled est false") {
        sea::itest::run_on_reactor([] {
            SeedTestContext ctx;

            sea::domain::Entity role;
            role.name       = "Role";
            role.table_name = "roles";
            role.fields     = {
                mk_fld("id",    sea::domain::FieldType::UUID,   true),
                mk_fld("label", sea::domain::FieldType::String, true),
            };
            sea::domain::SeedRecord r;
            r.values["label"] = std::string{"admin"};
            role.seeds = {r};

            sea::domain::Schema schema;
            schema.entities = {role};

            const auto result = ctx.run_seed(
                schema, sea::domain::SeedsMode::Always, /*seeds_enabled=*/false);

            REQUIRE(result.success);
            // Aucune insertion : la table reste vide.
            CHECK(ctx.count("Role") == 0);

        });
    }

} // TEST_SUITE


// ═══════════════════════════════════════════════════════════════
TEST_SUITE("SeedOrchestrator : macro hash [integration]") {

    // ───────────────────────────────────────────────────────────────
    // {{hash:value}} : un champ Password contenant {{hash:secret}} doit
    // être stocké HACHÉ, jamais en clair.
    //
    // On vérifie deux choses :
    //   - la valeur stockée n'est PAS le texte clair "s3cret" ;
    //   - elle a la forme d'un hash bcrypt (préfixe "$2", longueur 60).
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("la macro {{hash:}} stocke le mot de passe haché, pas en clair") {
        sea::itest::run_on_reactor([] {
            SeedTestContext ctx;

            sea::domain::Entity user;
            user.name       = "User";
            user.table_name = "users";
            user.fields     = {
                mk_fld("id",       sea::domain::FieldType::UUID,     true),
                mk_fld("name",     sea::domain::FieldType::String,   true),
                mk_fld("password", sea::domain::FieldType::Password, true),
            };

            sea::domain::SeedRecord seed;
            seed.values["name"]     = std::string{"alice"};
            seed.values["password"] = std::string{"{{hash:s3cret}}"};
            user.seeds = {seed};

            sea::domain::Schema schema;
            schema.entities = {user};

            const auto result = ctx.run_seed(schema, sea::domain::SeedsMode::Once);
            REQUIRE(result.success);
            REQUIRE(ctx.count("User") == 1);

            // Relit l'unique User et inspecte le champ password.
            const auto all = ctx.repository->find_all("User").get();
            REQUIRE(all.size() == 1);
            const std::string stored = str_field(all.front(), "password");

            // Jamais le texte clair.
            CHECK(stored != "s3cret");
            CHECK(stored != "{{hash:s3cret}}");

            // Forme d'un hash bcrypt : commence par "$2", longueur 60.
            REQUIRE(stored.size() == 60);
            CHECK(stored.substr(0, 2) == "$2");

        });
    }

} // TEST_SUITE


// ═══════════════════════════════════════════════════════════════
TEST_SUITE("SeedOrchestrator : macro REF [integration]") {

    // ───────────────────────────────────────────────────────────────
    // ${REF:alias} : un seed peut référencer un autre seed par alias.
    // L'orchestrateur résout la macro vers l'UUID réel du seed cible.
    //
    // Schéma : Author (1) ←── Book (n), via une FK author_id.
    //   - un seed Author avec alias "auteur1" ;
    //   - un seed Book dont author_id = "${REF:auteur1}".
    //
    // Après seeding, le Book inséré doit avoir author_id == l'UUID
    // réellement attribué à l'Author. Si la résolution échouait, soit
    // l'INSERT violerait la FK (échec), soit author_id contiendrait la
    // macro brute — les deux seraient détectés ici.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("la macro ${REF:} résout vers l'UUID réel de l'entité référencée") {
        sea::itest::run_on_reactor([] {
            SeedTestContext ctx;

            using sea::domain::FieldType;
            using sea::domain::Relation;
            using sea::domain::RelationKind;

            // Entité Author.
            sea::domain::Entity author;
            author.name       = "Author";
            author.table_name = "authors";
            author.fields     = {
                mk_fld("id",   FieldType::UUID,   true),
                mk_fld("name", FieldType::String, true),
            };

            sea::domain::SeedRecord author_seed;
            author_seed.alias            = "auteur1";   // alias référençable
            author_seed.values["name"]   = std::string{"Victor Hugo"};
            author.seeds = {author_seed};

            // Entité Book, avec FK author_id → authors.id.
            sea::domain::Entity book;
            book.name       = "Book";
            book.table_name = "books";

            auto author_fk = mk_fld("author_id", FieldType::UUID, true);
            book.fields = {
                mk_fld("id",    FieldType::UUID,   true),
                mk_fld("title", FieldType::String, true),
                author_fk,
            };

            Relation belongs;
            belongs.name          = "author";
            belongs.target_entity = "Author";
            belongs.kind          = RelationKind::BelongsTo;
            belongs.fk_column     = "author_id";
            book.relations = {belongs};

            sea::domain::SeedRecord book_seed;
            book_seed.values["title"]     = std::string{"Les Misérables"};
            book_seed.values["author_id"] = std::string{"${REF:auteur1}"};
            book.seeds = {book_seed};

            sea::domain::Schema schema;
            schema.entities = {author, book};

            const auto result = ctx.run_seed(schema, sea::domain::SeedsMode::Once);
            REQUIRE(result.success);
            REQUIRE(ctx.count("Author") == 1);
            REQUIRE(ctx.count("Book")   == 1);

            // UUID réellement attribué à l'auteur.
            const auto authors = ctx.repository->find_all("Author").get();
            REQUIRE(authors.size() == 1);
            const std::string author_uuid = str_field(authors.front(), "id");

            // Le Book doit pointer EXACTEMENT sur cet UUID.
            const auto books = ctx.repository->find_all("Book").get();
            REQUIRE(books.size() == 1);
            const std::string book_author_id = str_field(books.front(), "author_id");

            CHECK(book_author_id == author_uuid);
            // La macro brute ne doit jamais subsister.
            CHECK(book_author_id != "${REF:auteur1}");

        });
    }

} // TEST_SUITE


// ═══════════════════════════════════════════════════════════════
TEST_SUITE("SeedOrchestrator : pivots M2M [integration]") {

    // ───────────────────────────────────────────────────────────────
    // m2m_relations : un SeedRecord peut déclarer des associations M2M
    // vers d'autres seeds (par alias). L'orchestrateur insère les
    // lignes correspondantes dans la table pivot.
    //
    // Schéma : User ⟷ Role via pivot user_roles.
    //   - deux Role seedés, aliases "role_admin" et "role_viewer" ;
    //   - un User seedé (alias "user_alice") dont
    //     m2m_relations["roles"] = {role_admin, role_viewer}.
    //
    // CONTRAINTE de l'orchestrateur : le seed PORTEUR des m2m_relations
    // (ici User) doit lui-même avoir un alias. Sans alias, le M2M est
    // sauté (warning "SEEDS M2M skip: seed has no alias"), car
    // l'orchestrateur a besoin de référencer la ligne insérée pour
    // bâtir les lignes de pivot.
    //
    // Après seeding, total_pivot_rows doit valoir 2, et pivot_exists
    // doit confirmer les deux associations en base.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("les m2m_relations d'un seed génèrent les lignes de pivot") {
        sea::itest::run_on_reactor([] {
            SeedTestContext ctx;

            using sea::domain::FieldType;
            using sea::domain::Relation;
            using sea::domain::RelationKind;

            // Entité Role + deux seeds avec alias.
            sea::domain::Entity role;
            role.name       = "Role";
            role.table_name = "roles";
            role.fields     = {
                mk_fld("id",    FieldType::UUID,   true),
                mk_fld("label", FieldType::String, true),
            };
            sea::domain::SeedRecord role_admin;
            role_admin.alias            = "role_admin";
            role_admin.values["label"]  = std::string{"admin"};
            sea::domain::SeedRecord role_viewer;
            role_viewer.alias           = "role_viewer";
            role_viewer.values["label"] = std::string{"viewer"};
            role.seeds = {role_admin, role_viewer};

            // Entité User avec relation M2M vers Role.
            sea::domain::Entity user;
            user.name       = "User";
            user.table_name = "users";
            user.fields     = {
                mk_fld("id",   FieldType::UUID,   true),
                mk_fld("name", FieldType::String, true),
            };

            Relation roles_rel;
            roles_rel.name             = "roles";
            roles_rel.target_entity    = "Role";
            roles_rel.kind             = RelationKind::ManyToMany;
            roles_rel.pivot_table      = "user_roles";
            roles_rel.source_fk_column = "user_id";
            roles_rel.target_fk_column = "role_id";
            user.relations = {roles_rel};

            // Le seed User référence les deux rôles par alias.
            // IMPORTANT : le seed User doit lui-même porter un alias.
            // Sans alias, le SeedOrchestrator saute le traitement M2M
            // (warning "SEEDS M2M skip: seed has no alias") — il a besoin
            // de pouvoir référencer la ligne User insérée pour construire
            // les lignes de pivot.
            sea::domain::SeedRecord user_seed;
            user_seed.alias                   = "user_alice";
            user_seed.values["name"]          = std::string{"alice"};
            user_seed.m2m_relations["roles"]  = {"role_admin", "role_viewer"};
            user.seeds = {user_seed};

            sea::domain::Schema schema;
            schema.entities = {role, user};

            const auto result = ctx.run_seed(schema, sea::domain::SeedsMode::Once);

            REQUIRE(result.success);
            REQUIRE(ctx.count("Role") == 2);
            REQUIRE(ctx.count("User") == 1);

            // L'orchestrateur rapporte 2 lignes de pivot insérées.
            CHECK(result.total_pivot_rows == 2);

            // Vérification en base : récupère les UUID et confirme les
            // deux associations via pivot_exists.
            const auto users = ctx.repository->find_all("User").get();
            REQUIRE(users.size() == 1);
            const std::string user_uuid = str_field(users.front(), "id");

            const auto roles = ctx.repository->find_all("Role").get();
            REQUIRE(roles.size() == 2);

            // Pour chacun des deux rôles, l'association (user, role)
            // doit exister dans le pivot.
            for (const auto& role_rec : roles) {
                const std::string role_uuid = str_field(role_rec, "id");
                runtime::DynamicRecord link;
                link["user_id"] = user_uuid;
                link["role_id"] = role_uuid;
                CHECK(ctx.repository->pivot_exists("user_roles", link).get());
            }

        });
    }

} // TEST_SUITE


// ═══════════════════════════════════════════════════════════════
TEST_SUITE("SeedOrchestrator : modes de seeding [integration]") {

    // ───────────────────────────────────────────────────────────────
    // Mode Once : seed_all ne seede que si la table est vide. Un second
    // seed_all sur une table déjà peuplée ne doit RIEN ajouter.
    //
    // On exécute run_seed deux fois (deux orchestrateurs successifs sur
    // la même base jetable) avec le mode Once et on vérifie qu'après le
    // second passage, le compte n'a pas bougé.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("mode Once : un second seeding sur table peuplée n'ajoute rien") {
        sea::itest::run_on_reactor([] {
            SeedTestContext ctx;

            sea::domain::Entity role;
            role.name       = "Role";
            role.table_name = "roles";
            role.fields     = {
                mk_fld("id",    sea::domain::FieldType::UUID,   true),
                mk_fld("label", sea::domain::FieldType::String, true),
            };
            sea::domain::SeedRecord r;
            r.values["label"] = std::string{"admin"};
            role.seeds = {r};

            sea::domain::Schema schema;
            schema.entities = {role};

            // 1er seeding : insère.
            REQUIRE(ctx.run_seed(schema, sea::domain::SeedsMode::Once).success);
            REQUIRE(ctx.count("Role") == 1);

            // 2e seeding, mode Once, table déjà peuplée : aucune
            // insertion supplémentaire attendue.
            REQUIRE(ctx.run_seed(schema, sea::domain::SeedsMode::Once).success);
            CHECK(ctx.count("Role") == 1);

        });
    }

    // ───────────────────────────────────────────────────────────────
    // Mode Always : seed_all seede à chaque appel, même si la table
    // contient déjà des données. Un second passage doit donc AJOUTER
    // les seeds une nouvelle fois.
    //
    // C'est le pendant du test précédent : il prouve que le mode change
    // réellement le comportement.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("mode Always : un second seeding ré-insère les seeds") {
        sea::itest::run_on_reactor([] {
            SeedTestContext ctx;

            sea::domain::Entity role;
            role.name       = "Role";
            role.table_name = "roles";
            role.fields     = {
                mk_fld("id",    sea::domain::FieldType::UUID,   true),
                mk_fld("label", sea::domain::FieldType::String, true),
            };
            sea::domain::SeedRecord r;
            r.values["label"] = std::string{"admin"};
            role.seeds = {r};

            sea::domain::Schema schema;
            schema.entities = {role};

            // 1er seeding : 1 ligne.
            REQUIRE(ctx.run_seed(schema, sea::domain::SeedsMode::Always).success);
            REQUIRE(ctx.count("Role") == 1);

            // 2e seeding en mode Always : la ligne est ré-insérée → 2.
            REQUIRE(ctx.run_seed(schema, sea::domain::SeedsMode::Always).success);
            CHECK(ctx.count("Role") == 2);

        });
    }

} // TEST_SUITE