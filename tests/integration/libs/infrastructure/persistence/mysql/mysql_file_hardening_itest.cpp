// ═══════════════════════════════════════════════════════════════
// integration/libs/infrastructure/persistence/mysql/
//     mysql_file_hardening_itest.cpp
//
// Tests de DURCISSEMENT de la feature File.
//
// Contrairement à mysql_file_service_itest.cpp (qui valide le chemin
// heureux), ces tests sont construits pour CASSER le module et
// révéler des défauts. Chacun cible un angle mort identifié par
// relecture du code de FileService / FilesystemStorage.
//
// Périmètre, par priorité :
//   1. Path traversal      — storage_path malveillant doit être refusé
//   2. Atomicité           — INSERT échoué ne laisse pas de fichier orphelin
//   3. Under-flow compteur — release sans retain : que devient le fichier
//   4. OnDeleteFile::Restrict — fail-safe traité comme SetNull
//   5. Bornes de validation   — taille exactement à la limite
//   6. Extension dérivée      — sans point, multi-points, casse, dotfile
//   7. Contenu binaire réel   — round-trip sur les 256 valeurs d'octets
//
// IMPORTANT — lecture des résultats :
//   Si un de ces tests ÉCHOUE, ce n'est pas forcément le test qui a
//   tort : c'est peut-être un vrai défaut du module File à corriger.
//   Chaque assertion documente le comportement ATTENDU ; un écart
//   est un point à examiner, pas à faire taire.
//
// Pré-requis : MySQL de tests/docker-compose.test.yml en marche.
// ═══════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include "persistence/mysql/mysql_generic_repository.h"
#include "support/seastar_test_harness.h"
#include "support/mysql_test_fixture.h"

#include "persistence/mysql/mysql_bootstrapper.h"
#include "file_repository/filerepository.h"
#include "storage/filesystem_storage.h"
#include "fileservice.h"

#include "runtime/schema_runtime_registry.h"

#include "database_config.h"
#include "storage_config.h"
#include "file_field_config.h"
#include "file_metadata.h"
#include "schema.h"
#include "entity.h"
#include "field.h"
#include "field_type.h"

#include <seastar/core/future.hh>

#include <chrono>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>

namespace {

namespace mysql   = sea::infrastructure::persistence::mysql;
namespace persist = sea::infrastructure::persistence;
namespace storage = sea::infrastructure::storage;
namespace runtime = sea::infrastructure::runtime;
namespace app     = sea::application;

// ───────────────────────────────────────────────────────────────
// Helpers — redéclarés ici car ceux de mysql_file_service_itest.cpp
// vivent dans le namespace anonyme de CE fichier-là (propre à son
// unité de traduction, donc inaccessible ici). Pas de conflit de
// linkage : chaque namespace anonyme est distinct.
// ───────────────────────────────────────────────────────────────

// Dossier disque jetable (équivalent filesystem de ScopedDatabase).
class TempStorageDir {
public:
    TempStorageDir() {
        const auto base = std::filesystem::temp_directory_path();
        const auto unique =
            "sea_itest_hardening_"
            + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count());
        path_ = base / unique;
        std::filesystem::create_directories(path_);
    }
    ~TempStorageDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    TempStorageDir(const TempStorageDir&)            = delete;
    TempStorageDir& operator=(const TempStorageDir&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }
    [[nodiscard]] std::string string() const { return path_.string(); }

private:
    std::filesystem::path path_;
};

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
    config.migrations.mode                       = sea::domain::MigrationMode::Aggressive;
    config.migrations.create_database_if_missing = true;
    return config;
}

// Entité système sea_files (cf. mysql_file_service_itest.cpp pour
// l'explication détaillée — résumé : le repo générique doit pouvoir
// résoudre "sea_files" dans le registry).
sea::domain::Entity build_sea_files_entity()
{
    using sea::domain::Field;
    using sea::domain::FieldType;
    auto fld = [](const std::string& n, FieldType t, bool req) {
        Field f; f.name = n; f.type = t; f.required = req; return f;
    };
    sea::domain::Entity e;
    e.name = "sea_files";
    e.table_name = "sea_files";
    e.fields = {
        fld("id",              FieldType::UUID,      true),
        fld("original_name",   FieldType::String,    true),
        fld("mime_type",       FieldType::String,    true),
        fld("size_bytes",      FieldType::BigInt,    true),
        fld("storage_path",    FieldType::String,    true),
        fld("reference_count", FieldType::Int,       true),
        fld("created_at",      FieldType::Timestamp, false),
    };
    return e;
}

// Schéma avec une entité portant un champ File (déclenche sea_files).
sea::domain::Schema build_schema_with_file()
{
    using sea::domain::Entity;
    using sea::domain::Field;
    using sea::domain::FieldType;

    Field id_field;
    id_field.name = "id"; id_field.type = FieldType::UUID; id_field.required = true;
    Field name_field;
    name_field.name = "name"; name_field.type = FieldType::String; name_field.required = true;
    Field avatar_field;
    avatar_field.name = "avatar"; avatar_field.type = FieldType::File;
    sea::domain::FileFieldConfig fcfg;
    fcfg.storage_path = "users/avatars";
    avatar_field.file_config = fcfg;

    Entity user;
    user.name = "User"; user.table_name = "users";
    user.fields = {id_field, name_field, avatar_field};

    sea::domain::Schema schema;
    schema.entities = {user};
    return schema;
}

// ───────────────────────────────────────────────────────────────
// HardeningContext — base jetable + storage jetable + chaîne File.
// teardown en RAII (cf. les autres *TestContext).
// ───────────────────────────────────────────────────────────────
struct HardeningContext {
    sea::itest::MysqlTestFixture fixture;
    sea::itest::ScopedDatabase   scoped_db;
    TempStorageDir               temp_dir;

    std::unique_ptr<seastar::sharded<mysql::MysqlConnexionPool>> pool;
    std::shared_ptr<runtime::SchemaRuntimeRegistry>             registry;
    std::shared_ptr<mysql::MySQLGenericRepository>              generic_repo;
    std::shared_ptr<persist::FileRepository>                    file_repo;
    std::shared_ptr<storage::IFileStorage>                      file_storage;
    std::shared_ptr<app::FileService>                           file_service;

    HardeningContext()
        : scoped_db(fixture)
    {
        const auto schema = build_schema_with_file();
        const auto config = build_database_config(fixture);

        pool = fixture.make_pool().get();

        mysql::MysqlBootstrapper bootstrapper{
                                              config, schema, *pool, fixture.executor()};
        REQUIRE(bootstrapper.bootstrap().get().success);

        sea::domain::Schema registry_schema = schema;
        registry_schema.entities.push_back(build_sea_files_entity());
        registry = std::make_shared<runtime::SchemaRuntimeRegistry>();
        registry->register_schema(registry_schema);

        generic_repo = std::make_shared<mysql::MySQLGenericRepository>(
            *pool, registry, fixture.executor());
        file_repo = std::make_shared<persist::FileRepository>(generic_repo);

        sea::domain::StorageConfig scfg;
        scfg.backend   = sea::domain::StorageBackend::Filesystem;
        scfg.root_path = temp_dir.string();
        file_storage = std::make_shared<storage::FilesystemStorage>(scfg);

        file_service = std::make_shared<app::FileService>(
            file_repo, file_storage, fixture.executor());
    }

    ~HardeningContext() {
        if (pool) {
            try { pool->stop().get(); } catch (...) {}
        }
    }

    HardeningContext(const HardeningContext&)            = delete;
    HardeningContext& operator=(const HardeningContext&) = delete;
};

// Compte les fichiers réguliers présents sous la racine du storage,
// récursivement. Sert à détecter les fichiers orphelins.
std::size_t count_files_on_disk(const std::filesystem::path& root)
{
    std::size_t n = 0;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
         !ec && it != std::filesystem::recursive_directory_iterator();
         it.increment(ec)) {
        if (it->is_regular_file(ec)) {
            ++n;
        }
    }
    return n;
}

// Config File ouverte (aucune contrainte), storage_path donné.
sea::domain::FileFieldConfig config_with_path(const std::string& storage_path)
{
    sea::domain::FileFieldConfig cfg;
    cfg.storage_path = storage_path;
    cfg.on_delete    = sea::domain::OnDeleteFile::Cascade;
    return cfg;
}

} // namespace


// ═══════════════════════════════════════════════════════════════
TEST_SUITE("File hardening : path traversal [integration]") {

    // ───────────────────────────────────────────────────────────────
    // PRIORITÉ 1 — path traversal via storage_path.
    //
    // build_target_path concatène storage_path SANS validation. Toute
    // la sécurité repose sur FilesystemStorage::resolve_safe_path.
    //
    // Attendu : un storage_path contenant "../" qui sortirait de la
    // racine doit faire ÉCHOUER l'upload (StorageException). Le fichier
    // ne doit PAS apparaître hors du sandbox.
    //
    // Si ce test échoue (upload réussit), c'est une FAILLE DE SÉCURITÉ
    // critique : le sandbox laisse écrire n'importe où sur le disque.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("upload avec storage_path '../' hors sandbox doit être refusé") {
        sea::itest::run_on_reactor([] {
            HardeningContext ctx;

            // storage_path qui tente de remonter au-dessus de la racine.
            const auto evil = config_with_path("../../../../tmp/sea_evil");

            bool threw = false;
            try {
                ctx.file_service->upload(
                                    evil, "x.bin", "application/octet-stream", "payload").get();
            } catch (const std::exception&) {
                threw = true;
            }

            // L'upload DOIT échouer : le sandbox refuse la sortie.
            CHECK(threw);

            // Et rien ne doit être écrit hors de la racine du storage.
            // (On ne peut pas scanner tout le disque ; on vérifie au
            // moins qu'aucun fichier n'est apparu DANS la racine non
            // plus — l'écriture a été refusée avant.)
            CHECK(count_files_on_disk(ctx.temp_dir.path()) == 0);

            ctx.pool->stop().get();
            ctx.pool.reset();
        });
    }

    // ───────────────────────────────────────────────────────────────
    // Path absolu comme storage_path : doit aussi être refusé.
    // resolve_safe_path rejette explicitement les chemins absolus.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("upload avec storage_path absolu doit être refusé") {
        sea::itest::run_on_reactor([] {
            HardeningContext ctx;

            const auto evil = config_with_path("/tmp/sea_evil_absolute");

            bool threw = false;
            try {
                ctx.file_service->upload(
                                    evil, "x.bin", "application/octet-stream", "payload").get();
            } catch (const std::exception&) {
                threw = true;
            }
            CHECK(threw);

            ctx.pool->stop().get();
            ctx.pool.reset();
        });
    }

} // TEST_SUITE


// ═══════════════════════════════════════════════════════════════
TEST_SUITE("File hardening : atomicité storage <-> base [integration]") {

    // ───────────────────────────────────────────────────────────────
    // PRIORITÉ 2 — pas de fichier orphelin si l'INSERT échoue.
    //
    // upload écrit d'abord le fichier sur disque, PUIS insère dans
    // sea_files. Si l'INSERT échoue, le code fait storage->remove()
    // pour rollback (fileservice.cpp lignes 180-193).
    //
    // Levier pour faire échouer l'INSERT : un mime_type plus long que
    // la colonne VARCHAR(100). validate_upload ne filtre PAS le mime
    // quand allowed_mime_types est vide → le mime aberrant passe la
    // validation, l'écriture disque a lieu, puis l'INSERT est rejeté
    // par MySQL (mode strict).
    //
    // Attendu : upload lève une exception ET le fichier écrit est
    // rollback — aucun orphelin sur le disque.
    //
    // Si ce test échoue (fichier toujours présent), le rollback de
    // upload ne fonctionne pas : les uploads ratés pollueraient le
    // disque indéfiniment.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("un INSERT sea_files échoué ne laisse pas de fichier orphelin") {
        sea::itest::run_on_reactor([] {
            HardeningContext ctx;

            // mime_type de 200 caractères : > VARCHAR(100) de la colonne.
            const std::string oversized_mime(200, 'x');

            bool threw = false;
            try {
                ctx.file_service->upload(
                                    config_with_path("users/avatars"),
                                    "doc.bin",
                                    oversized_mime,
                                    "contenu qui ne doit pas rester orphelin").get();
            } catch (const std::exception&) {
                threw = true;
            }

            // L'upload doit échouer (INSERT rejeté par MySQL).
            CHECK(threw);

            // ROLLBACK : le fichier écrit avant l'INSERT doit avoir été
            // supprimé. Aucun fichier ne doit subsister sur le disque.
            CHECK(count_files_on_disk(ctx.temp_dir.path()) == 0);

            ctx.pool->stop().get();
            ctx.pool.reset();
        });
    }

} // TEST_SUITE


// ═══════════════════════════════════════════════════════════════
TEST_SUITE("File hardening : reference counting [integration]") {

    // ───────────────────────────────────────────────────────────────
    // PRIORITÉ 3 — under-flow : release sans retain préalable.
    //
    // upload crée le fichier avec reference_count = 0. Un release sans
    // retain correspondant est un défaut d'appelant.
    //
    // Comportement DURCI (release_reference_if_positive) : le décrément
    // est conditionnel et atomique — UPDATE ... WHERE reference_count
    // > 0. Sur un compteur à 0, aucune ligne n'est affectée :
    //   - release() renvoie false (refus explicite, warning loggé) ;
    //   - le compteur NE passe PAS sous zéro ;
    //   - le record sea_files et le fichier physique SURVIVENT.
    //
    // Ce test valide ce contrat. Si release renvoyait true ou si le
    // fichier disparaissait, le durcissement serait cassé.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("release sans retain est refusé : compteur et fichier préservés") {
        sea::itest::run_on_reactor([] {
            HardeningContext ctx;

            const auto result = ctx.file_service->upload(
                                                    config_with_path("users/avatars"),
                                                    "f.bin", "application/octet-stream", "data").get();

            // upload : reference_count == 0.
            {
                const auto m = ctx.file_repo->find_by_id(result.uuid).get();
                REQUIRE(m.has_value());
                REQUIRE(m->reference_count == 0);
            }

            // release SANS retain préalable, en mode Cascade.
            // Le décrément conditionnel ne touche pas un compteur à 0 :
            // release doit REFUSER (renvoyer false).
            const bool released = ctx.file_service->release(
                                                      result.uuid, sea::domain::OnDeleteFile::Cascade).get();
            CHECK_FALSE(released);

            // Le record sea_files existe toujours, et le compteur n'est
            // PAS passé sous zéro — il vaut encore 0.
            const auto after = ctx.file_repo->find_by_id(result.uuid).get();
            REQUIRE(after.has_value());
            CHECK(after->reference_count == 0);

            // Le fichier physique n'a pas été supprimé.
            CHECK(ctx.file_storage->exists(result.storage_path));

            ctx.pool->stop().get();
            ctx.pool.reset();
        });
    }

    // ───────────────────────────────────────────────────────────────
    // Complément : un release LÉGITIME (précédé d'un retain) ramène le
    // compteur à 0 et, en Cascade, supprime bien le fichier. On vérifie
    // que le durcissement n'a PAS cassé le chemin nominal.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("release après retain fonctionne toujours (chemin nominal intact)") {
        sea::itest::run_on_reactor([] {
            HardeningContext ctx;

            const auto result = ctx.file_service->upload(
                                                    config_with_path("users/avatars"),
                                                    "f.bin", "application/octet-stream", "data").get();

            // retain : compteur 0 -> 1.
            REQUIRE(ctx.file_service->retain(result.uuid).get());

            // release Cascade : compteur 1 -> 0, donc suppression.
            const bool released = ctx.file_service->release(
                                                      result.uuid, sea::domain::OnDeleteFile::Cascade).get();
            CHECK(released);

            // Compteur revenu à 0 + Cascade : record et fichier supprimés.
            CHECK_FALSE(ctx.file_repo->find_by_id(result.uuid).get().has_value());
            CHECK_FALSE(ctx.file_storage->exists(result.storage_path));

            ctx.pool->stop().get();
            ctx.pool.reset();
        });
    }

    // ───────────────────────────────────────────────────────────────
    // PRIORITÉ 4 — OnDeleteFile::Restrict est traité comme SetNull.
    //
    // release() avec Restrict : le code log un warning et bascule en
    // SetNull (fail-safe : préserve le fichier). On vérifie que le
    // fichier SURVIT après un release Restrict, même compteur à 0.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("release avec Restrict préserve le fichier (fail-safe SetNull)") {
        sea::itest::run_on_reactor([] {
            HardeningContext ctx;

            const auto result = ctx.file_service->upload(
                                                    config_with_path("users/avatars"),
                                                    "kept.bin", "application/octet-stream", "contenu").get();
            REQUIRE(ctx.file_service->retain(result.uuid).get());

            // release Restrict : doit être traité comme SetNull.
            const bool released = ctx.file_service->release(
                                                      result.uuid, sea::domain::OnDeleteFile::Restrict).get();
            CHECK(released);

            // Fail-safe : le fichier physique est PRÉSERVÉ.
            CHECK(ctx.file_storage->exists(result.storage_path));

            ctx.pool->stop().get();
            ctx.pool.reset();
        });
    }

} // TEST_SUITE


// ═══════════════════════════════════════════════════════════════
TEST_SUITE("File hardening : bornes de validation [integration]") {

    // ───────────────────────────────────────────────────────────────
    // PRIORITÉ 5 — la limite de taille est INCLUSIVE.
    //
    // accepts_size fait "size <= max_size_bytes". Donc une taille
    // EXACTEMENT égale à la limite doit être ACCEPTÉE, et limite+1
    // REFUSÉE. Les bugs vivent sur les bornes.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("validate_upload : la limite de taille est inclusive") {
        sea::itest::run_on_reactor([] {
            HardeningContext ctx;

            sea::domain::FileFieldConfig cfg;
            cfg.max_size_bytes = 1024;

            // Exactement 1024 : <= 1024 → accepté.
            const auto at_limit = app::FileService::validate_upload(
                cfg, "f.bin", "application/octet-stream", 1024);
            CHECK(at_limit.accepted);

            // 1025 : > 1024 → refusé.
            const auto over = app::FileService::validate_upload(
                cfg, "f.bin", "application/octet-stream", 1025);
            CHECK_FALSE(over.accepted);

            // 0 octet : <= 1024 → accepté (fichier vide autorisé par
            // la validation de taille ; à noter).
            const auto empty = app::FileService::validate_upload(
                cfg, "f.bin", "application/octet-stream", 0);
            CHECK(empty.accepted);

            ctx.pool->stop().get();
            ctx.pool.reset();
        });
    }

} // TEST_SUITE


// ═══════════════════════════════════════════════════════════════
TEST_SUITE("File hardening : extension dérivée du nom [integration]") {

    // ───────────────────────────────────────────────────────────────
    // PRIORITÉ 6 — extract_extension sur les cas limites.
    //
    // extract_extension utilise rfind('.'). On éprouve, via le filtre
    // d'extension de validate_upload, plusieurs cas tordus :
    //   - nom SANS point          → extension ""
    //   - nom MULTI-points        → seule la dernière partie compte
    //   - casse mixte             → comparaison insensible à la casse
    //   - nom finissant par '.'   → extension "" (pos == size-1)
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("validate_upload : extension multi-points prend la dernière") {
        sea::itest::run_on_reactor([] {
            HardeningContext ctx;

            sea::domain::FileFieldConfig cfg;
            cfg.allowed_extensions = {".gz"};   // filtre : .gz uniquement

            // "archive.tar.gz" → rfind('.') donne ".gz" → accepté.
            const auto multi = app::FileService::validate_upload(
                cfg, "archive.tar.gz", "application/gzip", 10);
            CHECK(multi.accepted);

            // "archive.tar" → extension ".tar", pas dans le filtre → refusé.
            const auto tar = app::FileService::validate_upload(
                cfg, "archive.tar", "application/x-tar", 10);
            CHECK_FALSE(tar.accepted);

            ctx.pool->stop().get();
            ctx.pool.reset();
        });
    }

    TEST_CASE("validate_upload : nom sans extension face à un filtre") {
        sea::itest::run_on_reactor([] {
            HardeningContext ctx;

            sea::domain::FileFieldConfig cfg;
            cfg.allowed_extensions = {".png"};

            // "README" : aucun point → extension "" → refusé par le filtre.
            const auto no_ext = app::FileService::validate_upload(
                cfg, "README", "text/plain", 10);
            CHECK_FALSE(no_ext.accepted);

            // "image." : finit par un point → extension "" → refusé.
            const auto trailing_dot = app::FileService::validate_upload(
                cfg, "image.", "image/png", 10);
            CHECK_FALSE(trailing_dot.accepted);

            ctx.pool->stop().get();
            ctx.pool.reset();
        });
    }

    TEST_CASE("validate_upload : extension en casse mixte est acceptée") {
        sea::itest::run_on_reactor([] {
            HardeningContext ctx;

            sea::domain::FileFieldConfig cfg;
            cfg.allowed_extensions = {".png"};

            // "PHOTO.PNG" : extract_extension passe en minuscules →
            // ".png" → doit être accepté malgré la casse d'origine.
            const auto upper = app::FileService::validate_upload(
                cfg, "PHOTO.PNG", "image/png", 10);
            CHECK(upper.accepted);

            // ".JpG" mixte aussi.
            const auto mixed = app::FileService::validate_upload(
                cfg, "Photo.JpG", "image/jpeg", 10);
            // Le filtre n'a que .png : .jpg n'y est pas → refusé.
            // (On vérifie surtout que la normalisation de casse marche :
            //  .JpG devient .jpg, comparé à .png → refus correct.)
            CHECK_FALSE(mixed.accepted);

            ctx.pool->stop().get();
            ctx.pool.reset();
        });
    }

} // TEST_SUITE


// ═══════════════════════════════════════════════════════════════
TEST_SUITE("File hardening : contenu binaire réel [integration]") {

    // ───────────────────────────────────────────────────────────────
    // PRIORITÉ 7 — round-trip d'un contenu binaire non trivial.
    //
    // On construit un payload couvrant les 256 valeurs d'octets
    // possibles, répété pour atteindre une taille conséquente (~64 Ko).
    // upload puis download : le contenu relu doit être identique BIT
    // POUR BIT — taille ET chaque octet.
    //
    // Cela éprouve que ni le storage, ni le passage en base, ni le
    // transport en std::string ne corrompent ou ne tronquent les
    // données (octet nul, octets > 127, etc.).
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("round-trip d'un contenu binaire couvrant les 256 valeurs d'octets") {
        sea::itest::run_on_reactor([] {
            HardeningContext ctx;

            // Payload : 256 valeurs d'octets répétées 256 fois → 64 Ko.
            std::string payload;
            payload.reserve(256 * 256);
            for (int rep = 0; rep < 256; ++rep) {
                for (int b = 0; b < 256; ++b) {
                    payload.push_back(static_cast<char>(b));
                }
            }
            REQUIRE(payload.size() == 256 * 256);

            const auto result = ctx.file_service->upload(
                                                    config_with_path("users/avatars"),
                                                    "blob.bin", "application/octet-stream", payload).get();

            CHECK(result.size_bytes == payload.size());

            // download : contenu identique bit pour bit.
            const auto dl = ctx.file_service->download(result.uuid).get();
            REQUIRE(dl.has_value());
            CHECK(dl->content.size() == payload.size());
            // Comparaison exacte de tout le contenu.
            CHECK(dl->content == payload);

            // Et la taille rapportée par le storage correspond aussi.
            CHECK(ctx.file_storage->size(result.storage_path) == payload.size());

            ctx.pool->stop().get();
            ctx.pool.reset();
        });
    }

} // TEST_SUITE