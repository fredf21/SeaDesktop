// ═══════════════════════════════════════════════════════════════
// integration/libs/infrastructure/persistence/mysql/
//     mysql_pagination_itest.cpp
//
// Tests d'INTÉGRATION des trois modes de pagination du repository :
// list_page, list_offset, list_cursor.
//
// ── Sémantique vérifiée (lue dans mysql_generic_repository.cpp) ──
//
// list_page   : page 1-indexée. offset interne = (page-1)*page_size.
//               page == 0 est normalisée en page 1 (ligne 1177).
//               PageResult.total = COUNT(*) global, indépendant de
//               la page demandée.
//
// list_offset : offset 0-indexé, passé tel quel à SQL. Pas de
//               normalisation. OffsetResult.total = COUNT(*) global.
//
// list_cursor : technique "limit + 1". after == nullopt → 1re page.
//               next_cursor = valeur du cursor_field du dernier
//               élément conservé ; nullopt s'il n'y a pas de page
//               suivante. Tri figé sur cursor_field.
//
// ── Comportement d'erreur (important) ──────────────────────────
//
// Sur entité inconnue, identifiant SQL invalide ou SQLException, le
// repository NE lève PAS : il renvoie un résultat VIDE
// (items vides, total = 0, next_cursor = nullopt). Les tests de cas
// limites vérifient ce contrat.
//
// Rappel : le repository ne valide PAS les bornes (c'est le rôle du
// handler HTTP). On teste donc le comportement BRUT du repository
// face à des bornes extrêmes, pas une quelconque validation.
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

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace mysql   = sea::infrastructure::persistence::mysql;
namespace runtime = sea::infrastructure::runtime;
namespace persist = sea::infrastructure::persistence;

// ───────────────────────────────────────────────────────────────
// Schéma : entité Item(id UUID, name String, rank Int). Le champ
// "name" sert de champ de tri et de cursor_field — c'est une string,
// donc next_cursor la transporte correctement.
// ───────────────────────────────────────────────────────────────
sea::domain::Schema build_item_schema()
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

    Field rank_field;
    rank_field.name = "rank";
    rank_field.type = FieldType::Int;
    rank_field.required = false;

    Entity item;
    item.name       = "Item";
    item.table_name = "items";
    item.fields     = {id_field, name_field, rank_field};

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
// PaginationTestContext
//
// Base jetable bootstrappée + repository. À construire DANS
// run_on_reactor. seed(n) insère n items nommés "item-00".."item-NN"
// (zéro-paddés pour que l'ordre lexicographique soit l'ordre
// numérique) avec rank = index.
// ───────────────────────────────────────────────────────────────
struct PaginationTestContext {
    sea::itest::MysqlTestFixture fixture;
    sea::itest::ScopedDatabase   scoped_db;

    std::unique_ptr<seastar::sharded<mysql::MysqlConnexionPool>> pool;
    std::shared_ptr<runtime::SchemaRuntimeRegistry>             registry;
    std::unique_ptr<mysql::MySQLGenericRepository>              repository;

    PaginationTestContext()
        : scoped_db(fixture)
    {
        const auto schema = build_item_schema();
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

    // Insère `count` items : name = "item-NN" (zéro-paddé), rank = i.
    void seed(int count) {
        for (int i = 0; i < count; ++i) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "item-%02d", i);

            runtime::DynamicRecord rec;
            rec["name"] = std::string{buf};
            rec["rank"] = std::int32_t{i};
            const auto created = repository->create("Item", rec).get();
            REQUIRE(created.has_value());
        }
    }

    // Helper : extrait le champ "name" d'un record.
    static std::string name_of(const runtime::DynamicRecord& rec) {
        const auto it = rec.find("name");
        REQUIRE(it != rec.end());
        REQUIRE(std::holds_alternative<std::string>(it->second));
        return std::get<std::string>(it->second);
    }

    // Destructeur RAII : arrête le pool. Garanti même si un test
    // échoue en plein milieu — une exception REQUIRE sauterait
    // par-dessus tout appel teardown() explicite, et le
    // sharded<MysqlConnexionPool> détruit sans stop() déclenche
    // l'assertion Seastar `_instances.empty()` (SIGABRT).
    ~PaginationTestContext() {
        if (pool) {
            try {
                pool->stop().get();
            } catch (...) {
                // Best-effort : un échec d'arrêt ne doit pas
                // masquer le résultat du test.
            }
        }
    }

    PaginationTestContext(const PaginationTestContext&)            = delete;
    PaginationTestContext& operator=(const PaginationTestContext&) = delete;
};

} // namespace


// ═══════════════════════════════════════════════════════════════
TEST_SUITE("MySQLGenericRepository list_page [integration]") {

    // ───────────────────────────────────────────────────────────────
    // Page nominale : 25 items, page_size 10. La page 1 doit contenir
    // 10 items et total doit valoir 25 (COUNT global, pas la taille de
    // la page).
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("list_page renvoie une page pleine et le total global") {
        sea::itest::run_on_reactor([] {
            PaginationTestContext ctx;
            ctx.seed(25);

            persist::PageRequest req;
            req.page       = 1;
            req.page_size  = 10;
            req.sort_field = "name";   // tri déterministe

            const auto result = ctx.repository->list_page("Item", req).get();

            CHECK(result.items.size() == 10);
            CHECK(result.total == 25);   // total global, indépendant de la page

            // Page 1 triée par name ASC : doit commencer à item-00.
            CHECK(PaginationTestContext::name_of(result.items.front()) == "item-00");
            CHECK(PaginationTestContext::name_of(result.items.back())  == "item-09");

        });
    }

    // ───────────────────────────────────────────────────────────────
    // Dernière page partielle : 25 items, page_size 10 → la page 3 ne
    // contient que 5 items, mais total reste 25.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("list_page : dernière page partielle, total inchangé") {
        sea::itest::run_on_reactor([] {
            PaginationTestContext ctx;
            ctx.seed(25);

            persist::PageRequest req;
            req.page       = 3;
            req.page_size  = 10;
            req.sort_field = "name";

            const auto result = ctx.repository->list_page("Item", req).get();

            CHECK(result.items.size() == 5);     // 25 - 2*10
            CHECK(result.total == 25);
            CHECK(PaginationTestContext::name_of(result.items.front()) == "item-20");
            CHECK(PaginationTestContext::name_of(result.items.back())  == "item-24");

        });
    }

    // ───────────────────────────────────────────────────────────────
    // Borne : page au-delà de la dernière → items vide, total correct.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("list_page : page au-delà du dernier renvoie vide, total intact") {
        sea::itest::run_on_reactor([] {
            PaginationTestContext ctx;
            ctx.seed(25);

            persist::PageRequest req;
            req.page      = 99;   // bien au-delà
            req.page_size = 10;

            const auto result = ctx.repository->list_page("Item", req).get();

            CHECK(result.items.empty());
            CHECK(result.total == 25);

        });
    }

    // ───────────────────────────────────────────────────────────────
    // Borne : page == 0. Le repository normalise page 0 → page 1
    // (ligne 1177). On vérifie ce comportement précis : page 0 doit
    // renvoyer EXACTEMENT la même chose que page 1.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("list_page : page 0 est normalisée en page 1") {
        sea::itest::run_on_reactor([] {
            PaginationTestContext ctx;
            ctx.seed(25);

            persist::PageRequest req;
            req.page       = 0;        // doit être traitée comme 1
            req.page_size  = 10;
            req.sort_field = "name";

            const auto result = ctx.repository->list_page("Item", req).get();

            CHECK(result.items.size() == 10);
            CHECK(result.total == 25);
            // Normalisée en page 1 : commence donc à item-00.
            CHECK(PaginationTestContext::name_of(result.items.front()) == "item-00");

        });
    }

    // ───────────────────────────────────────────────────────────────
    // Borne : entité inconnue → résultat vide, PAS d'exception.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("list_page : entité inconnue renvoie un résultat vide sans lever") {
        sea::itest::run_on_reactor([] {
            PaginationTestContext ctx;
            ctx.seed(5);

            persist::PageRequest req;
            req.page      = 1;
            req.page_size = 10;

            persist::PageResult result;
            REQUIRE_NOTHROW(
                result = ctx.repository->list_page("EntiteInexistante", req).get()
                );
            CHECK(result.items.empty());
            CHECK(result.total == 0);

        });
    }

    // ───────────────────────────────────────────────────────────────
    // Tri DESC : la page 1 triée par name DESC doit commencer par le
    // plus grand nom.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("list_page : tri descendant") {
        sea::itest::run_on_reactor([] {
            PaginationTestContext ctx;
            ctx.seed(15);

            persist::PageRequest req;
            req.page       = 1;
            req.page_size  = 5;
            req.sort_field = "name";
            req.sort_desc  = true;

            const auto result = ctx.repository->list_page("Item", req).get();

            REQUIRE(result.items.size() == 5);
            // DESC : item-14 en tête, item-10 en fin de page.
            CHECK(PaginationTestContext::name_of(result.items.front()) == "item-14");
            CHECK(PaginationTestContext::name_of(result.items.back())  == "item-10");

        });
    }

} // TEST_SUITE


// ═══════════════════════════════════════════════════════════════
TEST_SUITE("MySQLGenericRepository list_offset [integration]") {

    // ───────────────────────────────────────────────────────────────
    // Offset nominal : 20 items. offset 5, limit 10 → 10 items à partir
    // du 6e (item-05), total global = 20.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("list_offset renvoie la tranche demandée et le total global") {
        sea::itest::run_on_reactor([] {
            PaginationTestContext ctx;
            ctx.seed(20);

            persist::OffsetRequest req;
            req.offset     = 5;
            req.limit      = 10;
            req.sort_field = "name";

            const auto result = ctx.repository->list_offset("Item", req).get();

            CHECK(result.items.size() == 10);
            CHECK(result.total == 20);
            // offset 0-indexé : la tranche commence à item-05.
            CHECK(PaginationTestContext::name_of(result.items.front()) == "item-05");
            CHECK(PaginationTestContext::name_of(result.items.back())  == "item-14");

        });
    }

    // ───────────────────────────────────────────────────────────────
    // Borne : offset au-delà du total → items vide, total correct.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("list_offset : offset au-delà du total renvoie vide") {
        sea::itest::run_on_reactor([] {
            PaginationTestContext ctx;
            ctx.seed(20);

            persist::OffsetRequest req;
            req.offset = 100;   // bien au-delà des 20 items
            req.limit  = 10;

            const auto result = ctx.repository->list_offset("Item", req).get();

            CHECK(result.items.empty());
            CHECK(result.total == 20);

        });
    }

    // ───────────────────────────────────────────────────────────────
    // Borne : limit dépasse le nombre de lignes restantes → on récupère
    // seulement ce qui reste.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("list_offset : limit plus grande que le reste tronque au disponible") {
        sea::itest::run_on_reactor([] {
            PaginationTestContext ctx;
            ctx.seed(20);

            persist::OffsetRequest req;
            req.offset     = 15;
            req.limit      = 50;        // demande 50, il n'en reste que 5
            req.sort_field = "name";

            const auto result = ctx.repository->list_offset("Item", req).get();

            CHECK(result.items.size() == 5);
            CHECK(result.total == 20);
            CHECK(PaginationTestContext::name_of(result.items.front()) == "item-15");

        });
    }

    // ───────────────────────────────────────────────────────────────
    // Borne : offset 0 = début de la collection.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("list_offset : offset 0 commence au premier élément") {
        sea::itest::run_on_reactor([] {
            PaginationTestContext ctx;
            ctx.seed(10);

            persist::OffsetRequest req;
            req.offset     = 0;
            req.limit      = 3;
            req.sort_field = "name";

            const auto result = ctx.repository->list_offset("Item", req).get();

            REQUIRE(result.items.size() == 3);
            CHECK(PaginationTestContext::name_of(result.items.front()) == "item-00");

        });
    }

} // TEST_SUITE


// ═══════════════════════════════════════════════════════════════
TEST_SUITE("MySQLGenericRepository list_cursor [integration]") {

    // ───────────────────────────────────────────────────────────────
    // Première page : after = nullopt. 12 items, limit 5 → 5 items, et
    // un next_cursor non nul (il reste des pages).
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("list_cursor : première page renvoie limit items et un next_cursor") {
        sea::itest::run_on_reactor([] {
            PaginationTestContext ctx;
            ctx.seed(12);

            persist::CursorRequest req;
            req.after        = std::nullopt;   // première page
            req.limit        = 5;
            req.cursor_field = "name";

            const auto result = ctx.repository->list_cursor("Item", req).get();

            CHECK(result.items.size() == 5);
            CHECK(PaginationTestContext::name_of(result.items.front()) == "item-00");
            CHECK(PaginationTestContext::name_of(result.items.back())  == "item-04");

            // Il reste 7 items : next_cursor doit être renseigné et
            // pointer sur le dernier élément vu (item-04).
            REQUIRE(result.next_cursor.has_value());
            CHECK(*result.next_cursor == "item-04");

        });
    }

    // ───────────────────────────────────────────────────────────────
    // Pagination complète : on enchaîne les pages via next_cursor et on
    // vérifie qu'on parcourt EXACTEMENT tous les items, une seule fois,
    // dans l'ordre, sans trou ni doublon.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("list_cursor : enchaîner les curseurs parcourt tout sans trou ni doublon") {
        sea::itest::run_on_reactor([] {
            PaginationTestContext ctx;
            ctx.seed(12);

            std::vector<std::string> seen;
            std::optional<std::string> cursor = std::nullopt;

            // Garde-fou : au pire 100 itérations pour éviter une boucle
            // infinie si next_cursor ne se vidait jamais (ce serait un
            // bug — et ce garde-fou le transformerait en échec visible).
            for (int guard = 0; guard < 100; ++guard) {
                persist::CursorRequest req;
                req.after        = cursor;
                req.limit        = 5;
                req.cursor_field = "name";

                const auto page = ctx.repository->list_cursor("Item", req).get();
                for (const auto& rec : page.items) {
                    seen.push_back(PaginationTestContext::name_of(rec));
                }

                if (!page.next_cursor.has_value()) {
                    break;   // dernière page atteinte
                }
                cursor = page.next_cursor;
            }

            // 12 items vus, dans l'ordre, exactement une fois chacun.
            REQUIRE(seen.size() == 12);
            for (int i = 0; i < 12; ++i) {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "item-%02d", i);
                CHECK(seen[static_cast<std::size_t>(i)] == std::string{buf});
            }

        });
    }

    // ───────────────────────────────────────────────────────────────
    // Dernière page : quand le nombre d'items restants est <= limit,
    // next_cursor doit être nullopt (technique limit+1 : pas de ligne
    // supplémentaire lue).
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("list_cursor : la dernière page a un next_cursor nul") {
        sea::itest::run_on_reactor([] {
            PaginationTestContext ctx;
            ctx.seed(5);   // exactement = limit

            persist::CursorRequest req;
            req.after        = std::nullopt;
            req.limit        = 5;
            req.cursor_field = "name";

            const auto result = ctx.repository->list_cursor("Item", req).get();

            CHECK(result.items.size() == 5);
            // 5 items, limit 5 : aucune page suivante.
            CHECK_FALSE(result.next_cursor.has_value());

        });
    }

    // ───────────────────────────────────────────────────────────────
    // Borne : after pointant sur une valeur APRÈS le dernier élément
    // existant → aucune ligne ne suit, items vide, next_cursor nul.
    // Pas un curseur "invalide" au sens syntaxique, mais un curseur qui
    // ne correspond à aucune suite — cas réel quand des données ont été
    // supprimées entre deux pages.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("list_cursor : un after au-delà du dernier élément renvoie vide") {
        sea::itest::run_on_reactor([] {
            PaginationTestContext ctx;
            ctx.seed(10);

            persist::CursorRequest req;
            // "item-99" est lexicographiquement après tous les items
            // existants (item-00 .. item-09).
            req.after        = std::string{"item-99"};
            req.limit        = 5;
            req.cursor_field = "name";

            const auto result = ctx.repository->list_cursor("Item", req).get();

            CHECK(result.items.empty());
            CHECK_FALSE(result.next_cursor.has_value());

        });
    }

    // ───────────────────────────────────────────────────────────────
    // Borne : cursor_field invalide comme identifiant SQL → le
    // repository renvoie un résultat vide sans lever (validation
    // d'identifiant échouée).
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("list_cursor : cursor_field invalide renvoie vide sans lever") {
        sea::itest::run_on_reactor([] {
            PaginationTestContext ctx;
            ctx.seed(10);

            persist::CursorRequest req;
            req.after        = std::nullopt;
            req.limit        = 5;
            // Identifiant SQL invalide (backtick + espace) : doit être
            // rejeté par validate_sql_identifier.
            req.cursor_field = "name`; DROP TABLE items;--";

            persist::CursorResult result;
            REQUIRE_NOTHROW(
                result = ctx.repository->list_cursor("Item", req).get()
                );
            CHECK(result.items.empty());
            CHECK_FALSE(result.next_cursor.has_value());

            // La table doit toujours exister et contenir ses 10 items :
            // l'identifiant malveillant n'a pas été interprété.
            CHECK(ctx.repository->count("Item").get() == 10);

        });
    }

} // TEST_SUITE