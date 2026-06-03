// ═══════════════════════════════════════════════════════════════
// integration/libs/infrastructure/persistence/mysql/
//     mysql_file_service_itest.cpp
//
// Tests d'INTÉGRATION de la feature File : FileRepository et
// FileService.
//
// ── Deux composants, deux couches ──────────────────────────────
//
//   FileRepository  (sea_infrastructure) : persiste les métadonnées
//                   dans la table sea_files via IGenericRepository.
//
//   FileService     (sea_application) : orchestre le cycle de vie
//                   complet — IFileStorage (disque) + FileRepository
//                   (base) ensemble. C'est un vrai test multi-couches.
//
// Pour atteindre FileService, la cible sea_integration_tests linke
// désormais Sea::application (en plus de Sea::infrastructure).
//
// ── Ce que ces tests éprouvent réellement ──────────────────────
//
// Le round-trip complet : upload → fichier RÉELLEMENT écrit sur
// disque + métadonnée RÉELLEMENT insérée dans sea_files ; download →
// le contenu relu correspond bit pour bit. Et le reference counting :
// retain/release et la cascade de suppression selon OnDeleteFile.
//
// ── Infrastructure de test ─────────────────────────────────────
//
//   - Base MySQL jetable : MysqlTestFixture (comme partout).
//   - Dossier disque jetable : TempStorageDir ci-dessous, l'équivalent
//     filesystem de ScopedDatabase — créé sous temp_directory_path(),
//     supprimé récursivement à la destruction.
//
// La table sea_files n'est créée par le bootstrapper QUE si le
// schéma contient un champ FieldType::File (has_file_fields()). Le
// schéma de test inclut donc une entité avec un tel champ.
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
#include <cstdint>
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
// TempStorageDir
//
// Dossier disque jetable, équivalent filesystem de ScopedDatabase.
// Créé à la construction sous temp_directory_path(), supprimé
// récursivement à la destruction — y compris si une exception
// traverse (donc même quand une assertion doctest échoue).
// ───────────────────────────────────────────────────────────────
class TempStorageDir {
public:
    TempStorageDir() {
        const auto base = std::filesystem::temp_directory_path();
        const auto unique =
            "sea_itest_storage_"
            + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count());
        path_ = base / unique;
        std::filesystem::create_directories(path_);
    }

    ~TempStorageDir() {
        // Nettoyage best-effort : un échec de suppression ne doit
        // pas masquer le résultat du test.
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    TempStorageDir(const TempStorageDir&)            = delete;
    TempStorageDir& operator=(const TempStorageDir&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }
    [[nodiscard]] std::string string() const {
        return path_.string();
    }

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

// ───────────────────────────────────────────────────────────────
// build_schema_with_file
//
// Schéma contenant une entité avec un champ FieldType::File. La
// présence de ce champ est ce qui déclenche, côté bootstrapper, la
// création de la table sea_files (has_file_fields()).
// ───────────────────────────────────────────────────────────────
sea::domain::Schema build_schema_with_file()
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

    // Champ File : porte une FileFieldConfig. C'est lui qui fait
    // que has_file_fields() == true et que sea_files est créée.
    Field avatar_field;
    avatar_field.name = "avatar";
    avatar_field.type = FieldType::File;
    sea::domain::FileFieldConfig fcfg;
    fcfg.storage_path = "users/avatars";
    fcfg.on_delete    = sea::domain::OnDeleteFile::Cascade;
    avatar_field.file_config = fcfg;

    Entity user;
    user.name       = "User";
    user.table_name = "users";
    user.fields     = {id_field, name_field, avatar_field};

    sea::domain::Schema schema;
    schema.entities = {user};
    return schema;
}

// ───────────────────────────────────────────────────────────────
// build_sea_files_entity
//
// Décrit la table SYSTÈME sea_files comme une Entity du domaine.
//
// Pourquoi c'est nécessaire : FileRepository::insert appelle
// repo_->create("sea_files", ...). Le MySQLGenericRepository
// résout "sea_files" dans le SchemaRuntimeRegistry pour connaître
// les colonnes et leurs types — sans entrée registry, create()
// renvoie nullopt et l'insert échoue.
//
// sea_files n'est PAS une entité du schéma utilisateur (c'est une
// table système créée par le bootstrapper). Le test l'ajoute donc
// à un schéma dédié au registry (cf. FileTestContext), distinct du
// schéma passé au bootstrapper.
//
// Les FieldType reflètent exactement la DDL de SeaFilesTable :
//   id              BINARY(16)  → UUID   (déclenche UUID_TO_BIN)
//   original_name   VARCHAR     → String
//   mime_type       VARCHAR     → String
//   size_bytes      BIGINT      → BigInt
//   storage_path    VARCHAR     → String
//   reference_count INT         → Int
//   created_at      TIMESTAMP   → Timestamp
// ───────────────────────────────────────────────────────────────
sea::domain::Entity build_sea_files_entity()
{
    using sea::domain::Field;
    using sea::domain::FieldType;

    auto fld = [](const std::string& name, FieldType type, bool required) {
        Field f;
        f.name     = name;
        f.type     = type;
        f.required = required;
        return f;
    };

    sea::domain::Entity e;
    e.name       = "sea_files";   // nom == table : create("sea_files") résout ici
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


// Assemble : base jetable bootstrappée (avec sea_files) + dossier
// disque jetable + FilesystemStorage + FileRepository + FileService.
// À construire DANS run_on_reactor.
// ───────────────────────────────────────────────────────────────
struct FileTestContext {
    sea::itest::MysqlTestFixture fixture;
    sea::itest::ScopedDatabase   scoped_db;
    TempStorageDir               temp_dir;

    std::unique_ptr<seastar::sharded<mysql::MysqlConnexionPool>> pool;
    std::shared_ptr<runtime::SchemaRuntimeRegistry>             registry;
    std::shared_ptr<mysql::MySQLGenericRepository>              generic_repo;
    std::shared_ptr<persist::FileRepository>                    file_repo;
    std::shared_ptr<storage::IFileStorage>                      file_storage;
    std::shared_ptr<app::FileService>                           file_service;

    FileTestContext()
        : scoped_db(fixture)
    {
        const auto schema = build_schema_with_file();
        const auto config = build_database_config(fixture);

        pool = fixture.make_pool().get();

        // Bootstrap : crée la table users ET la table sea_files
        // (déclenchée par le champ File de l'entité User).
        mysql::MysqlBootstrapper bootstrapper{
                                              config, schema, *pool, fixture.executor()};
        REQUIRE(bootstrapper.bootstrap().get().success);

        // Registry : SchemaRuntimeRegistry n'expose que
        // register_schema(Schema). On ne peut pas enregistrer une
        // entité isolée. On construit donc un schéma DÉDIÉ au
        // registry = schéma utilisateur + entité système sea_files.
        //
        // Important : ce schéma enrichi n'est PAS celui passé au
        // bootstrapper. Le bootstrapper reçoit le schéma utilisateur
        // seul ; il crée sea_files lui-même (via SeaFilesTable,
        // déclenché par has_file_fields()). Lui ajouter sea_files
        // comme entité le ferait tenter de créer la table deux fois.
        sea::domain::Schema registry_schema = schema;
        registry_schema.entities.push_back(build_sea_files_entity());

        registry = std::make_shared<runtime::SchemaRuntimeRegistry>();
        registry->register_schema(registry_schema);

        generic_repo = std::make_shared<mysql::MySQLGenericRepository>(
            *pool, registry, fixture.executor());

        // FileRepository : wrappe le repo générique pour sea_files.
        file_repo = std::make_shared<persist::FileRepository>(generic_repo);

        // FilesystemStorage : pointe sur le dossier jetable.
        sea::domain::StorageConfig scfg;
        scfg.backend   = sea::domain::StorageBackend::Filesystem;
        scfg.root_path = temp_dir.string();
        file_storage = std::make_shared<storage::FilesystemStorage>(scfg);

        // FileService : orchestre repo + storage.
        file_service = std::make_shared<app::FileService>(
            file_repo, file_storage, fixture.executor());
    }

    // Destructeur : arrête le pool. Garanti même si un test a
    // échoué en plein milieu — une exception REQUIRE sauterait
    // par-dessus tout appel teardown() explicite, et le
    // sharded<MysqlConnexionPool> détruit sans stop() déclenche
    // l'assertion Seastar `_instances.empty()` (SIGABRT).
    ~FileTestContext() {
        if (pool) {
            try {
                pool->stop().get();
            } catch (...) {
                // Best-effort : un échec d'arrêt ne doit pas
                // masquer le résultat du test.
            }
        }
    }

    FileTestContext(const FileTestContext&)            = delete;
    FileTestContext& operator=(const FileTestContext&) = delete;
};

// FileFieldConfig minimale sans contrainte (validate_upload accepte
// tout). Les tests qui veulent éprouver la validation construisent
// leur propre config restreinte localement.
sea::domain::FileFieldConfig open_config()
{
    sea::domain::FileFieldConfig cfg;
    cfg.storage_path = "users/avatars";
    cfg.on_delete    = sea::domain::OnDeleteFile::Cascade;
    return cfg;
}

} // namespace


// ═══════════════════════════════════════════════════════════════
TEST_SUITE("FileRepository [integration]") {

    // ───────────────────────────────────────────────────────────────
    // insert + find_by_id : un FileMetadata inséré dans sea_files doit
    // être relu à l'identique sur tous ses champs.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("FileRepository insère et relit un FileMetadata") {
        sea::itest::run_on_reactor([] {
            FileTestContext ctx;

            sea::domain::FileMetadata meta;
            meta.id              = "11111111-1111-4111-8111-111111111111";
            meta.original_name   = "photo.png";
            meta.mime_type       = "image/png";
            meta.size_bytes      = 2048;
            meta.storage_path    = "users/avatars/11111111.png";
            meta.reference_count = 0;

            const bool inserted = ctx.file_repo->insert(meta).get();
            CHECK(inserted);

            const auto fetched = ctx.file_repo->find_by_id(meta.id).get();
            REQUIRE(fetched.has_value());
            CHECK(fetched->id            == meta.id);
            CHECK(fetched->original_name == "photo.png");
            CHECK(fetched->mime_type     == "image/png");
            CHECK(fetched->size_bytes    == 2048);
            CHECK(fetched->storage_path  == "users/avatars/11111111.png");
            CHECK(fetched->reference_count == 0);

        });
    }

    // ───────────────────────────────────────────────────────────────
    // find_by_id sur un UUID inconnu → nullopt, sans lever.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("FileRepository::find_by_id renvoie nullopt pour un UUID inconnu") {
        sea::itest::run_on_reactor([] {
            FileTestContext ctx;

            const auto fetched = ctx.file_repo->find_by_id(
                                                  "99999999-9999-4999-8999-999999999999").get();
            CHECK_FALSE(fetched.has_value());

        });
    }

    // ───────────────────────────────────────────────────────────────
    // add_reference / release_reference : le reference_count suit les
    // appels, et on le vérifie en relisant le record.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("FileRepository : add_reference et release_reference suivent le compteur") {
        sea::itest::run_on_reactor([] {
            FileTestContext ctx;

            sea::domain::FileMetadata meta;
            meta.id              = "22222222-2222-4222-8222-222222222222";
            meta.original_name   = "doc.pdf";
            meta.mime_type       = "application/pdf";
            meta.size_bytes      = 100;
            meta.storage_path    = "users/avatars/22222222.pdf";
            meta.reference_count = 0;
            REQUIRE(ctx.file_repo->insert(meta).get());

            // Deux retain → compteur à 2.
            REQUIRE(ctx.file_repo->add_reference(meta.id).get());
            REQUIRE(ctx.file_repo->add_reference(meta.id).get());
            {
                const auto m = ctx.file_repo->find_by_id(meta.id).get();
                REQUIRE(m.has_value());
                CHECK(m->reference_count == 2);
            }

            // Un release → compteur à 1.
            REQUIRE(ctx.file_repo->release_reference(meta.id).get());
            {
                const auto m = ctx.file_repo->find_by_id(meta.id).get();
                REQUIRE(m.has_value());
                CHECK(m->reference_count == 1);
            }

        });
    }

    // ───────────────────────────────────────────────────────────────
    // delete_row : supprime le record sea_files ; find_by_id ne le
    // retrouve plus.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("FileRepository::delete_row supprime le record sea_files") {
        sea::itest::run_on_reactor([] {
            FileTestContext ctx;

            sea::domain::FileMetadata meta;
            meta.id              = "33333333-3333-4333-8333-333333333333";
            meta.original_name   = "tmp.bin";
            meta.mime_type       = "application/octet-stream";
            meta.size_bytes      = 1;
            meta.storage_path    = "users/avatars/33333333.bin";
            meta.reference_count = 0;
            REQUIRE(ctx.file_repo->insert(meta).get());

            const bool deleted = ctx.file_repo->delete_row(meta.id).get();
            CHECK(deleted);

            CHECK_FALSE(ctx.file_repo->find_by_id(meta.id).get().has_value());

        });
    }

} // TEST_SUITE


// ═══════════════════════════════════════════════════════════════
TEST_SUITE("FileService : upload / download [integration]") {

    // ───────────────────────────────────────────────────────────────
    // LE round-trip central : upload puis download.
    //
    // upload doit : écrire le fichier RÉELLEMENT sur disque + insérer
    // la métadonnée dans sea_files. download doit relire un contenu
    // identique BIT POUR BIT.
    //
    // On vérifie les trois couches :
    //   - le retour UploadResult (uuid, size) ;
    //   - le contenu relu par download ;
    //   - le fichier physiquement présent dans le storage.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("upload écrit fichier + métadonnée, download relit à l'identique") {
        sea::itest::run_on_reactor([] {
            FileTestContext ctx;

            const std::string content = "binary\0content\x01\x02bytes";
            // Construit explicitement avec une taille fixe (le \0 au
            // milieu ne doit pas tronquer).
            const std::string payload(content.data(), 21);

            const auto result = ctx.file_service->upload(
                                                    open_config(),
                                                    /*original_name*/ "avatar.png",
                                                    /*mime_type*/     "image/png",
                                                    payload).get();

            // UploadResult cohérent.
            CHECK_FALSE(result.uuid.empty());
            CHECK(result.uuid.size() == 36);
            CHECK(result.size_bytes == payload.size());
            CHECK(result.mime_type == "image/png");

            // download : contenu identique bit pour bit.
            const auto dl = ctx.file_service->download(result.uuid).get();
            REQUIRE(dl.has_value());
            CHECK(dl->content == payload);
            CHECK(dl->content.size() == payload.size());
            CHECK(dl->metadata.original_name == "avatar.png");
            CHECK(dl->metadata.mime_type == "image/png");

            // Le fichier existe physiquement dans le storage.
            CHECK(ctx.file_storage->exists(result.storage_path));
            CHECK(ctx.file_storage->size(result.storage_path) == payload.size());

        });
    }

    // ───────────────────────────────────────────────────────────────
    // upload initialise reference_count à 0 (le handler doit appeler
    // retain ensuite). On le vérifie via le FileRepository.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("upload crée la métadonnée avec reference_count = 0") {
        sea::itest::run_on_reactor([] {
            FileTestContext ctx;

            const auto result = ctx.file_service->upload(
                                                    open_config(), "f.txt", "text/plain", "hello").get();

            const auto meta = ctx.file_repo->find_by_id(result.uuid).get();
            REQUIRE(meta.has_value());
            CHECK(meta->reference_count == 0);

        });
    }

    // ───────────────────────────────────────────────────────────────
    // download sur un UUID inconnu → nullopt (le handler produira 404).
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("download renvoie nullopt pour un UUID inconnu") {
        sea::itest::run_on_reactor([] {
            FileTestContext ctx;

            const auto dl = ctx.file_service->download(
                                                "00000000-0000-4000-8000-000000000000").get();
            CHECK_FALSE(dl.has_value());

        });
    }

    // ───────────────────────────────────────────────────────────────
    // Validation : un upload qui dépasse max_size doit être rejeté.
    // validate_upload est statique — on la teste directement, c'est le
    // contrat que le handler HTTP utilise avant d'appeler upload().
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("validate_upload rejette un fichier trop gros") {
        sea::itest::run_on_reactor([] {
            FileTestContext ctx;

            sea::domain::FileFieldConfig cfg;
            cfg.max_size_bytes = 1024;   // limite : 1 Ko

            // 2 Ko : au-dessus de la limite → refusé.
            const auto too_big = app::FileService::validate_upload(
                cfg, "big.bin", "application/octet-stream", 2048);
            CHECK_FALSE(too_big.accepted);
            CHECK_FALSE(too_big.error_message.empty());

            // 512 o : sous la limite → accepté.
            const auto ok = app::FileService::validate_upload(
                cfg, "small.bin", "application/octet-stream", 512);
            CHECK(ok.accepted);

        });
    }

    // ───────────────────────────────────────────────────────────────
    // Validation : un type MIME hors liste blanche doit être rejeté.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("validate_upload rejette un type MIME non autorisé") {
        sea::itest::run_on_reactor([] {
            FileTestContext ctx;

            sea::domain::FileFieldConfig cfg;
            cfg.allowed_mime_types = {"image/png", "image/jpeg"};

            // application/pdf : hors liste → refusé.
            const auto rejected = app::FileService::validate_upload(
                cfg, "doc.pdf", "application/pdf", 100);
            CHECK_FALSE(rejected.accepted);

            // image/png : dans la liste → accepté.
            const auto accepted = app::FileService::validate_upload(
                cfg, "img.png", "image/png", 100);
            CHECK(accepted.accepted);

        });
    }

} // TEST_SUITE


// ═══════════════════════════════════════════════════════════════
TEST_SUITE("FileService : reference counting [integration]") {

    // ───────────────────────────────────────────────────────────────
    // retain incrémente le reference_count. On le vérifie via le repo.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("retain incrémente le reference_count") {
        sea::itest::run_on_reactor([] {
            FileTestContext ctx;

            const auto result = ctx.file_service->upload(
                                                    open_config(), "f.txt", "text/plain", "data").get();

            REQUIRE(ctx.file_service->retain(result.uuid).get());

            const auto meta = ctx.file_repo->find_by_id(result.uuid).get();
            REQUIRE(meta.has_value());
            CHECK(meta->reference_count == 1);

        });
    }

    // ───────────────────────────────────────────────────────────────
    // retain sur un UUID inconnu → false (le handler doit alors faire
    // un rollback).
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("retain renvoie false pour un UUID inconnu") {
        sea::itest::run_on_reactor([] {
            FileTestContext ctx;

            const bool ok = ctx.file_service->retain(
                                                "00000000-0000-4000-8000-000000000000").get();
            CHECK_FALSE(ok);

        });
    }

    // ───────────────────────────────────────────────────────────────
    // release en mode Cascade : quand le reference_count retombe à 0,
    // le record sea_files ET le fichier physique doivent disparaître.
    //
    // Scénario : upload (count 0) → retain (count 1) → release Cascade
    // (count 0 → suppression). On vérifie les deux disparitions.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("release Cascade supprime record et fichier quand le compteur atteint 0") {
        sea::itest::run_on_reactor([] {
            FileTestContext ctx;

            const auto result = ctx.file_service->upload(
                                                    open_config(), "doomed.bin", "application/octet-stream",
                                                    "to be deleted").get();
            const std::string uuid = result.uuid;
            const std::string path = result.storage_path;

            // Une référence, puis on la relâche.
            REQUIRE(ctx.file_service->retain(uuid).get());

            const bool released = ctx.file_service->release(
                                                      uuid, sea::domain::OnDeleteFile::Cascade).get();
            CHECK(released);

            // Cascade + compteur 0 : le record sea_files a disparu.
            CHECK_FALSE(ctx.file_repo->find_by_id(uuid).get().has_value());
            // ... et le fichier physique aussi.
            CHECK_FALSE(ctx.file_storage->exists(path));

        });
    }

    // ───────────────────────────────────────────────────────────────
    // release Cascade quand le compteur ne retombe PAS à 0 : le fichier
    // et le record doivent SUBSISTER.
    //
    // Scénario : upload → retain ×2 (count 2) → release Cascade
    // (count 1, > 0). Rien ne doit être supprimé.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("release Cascade ne supprime rien tant que le compteur reste > 0") {
        sea::itest::run_on_reactor([] {
            FileTestContext ctx;

            const auto result = ctx.file_service->upload(
                                                    open_config(), "shared.bin", "application/octet-stream",
                                                    "shared content").get();
            const std::string uuid = result.uuid;
            const std::string path = result.storage_path;

            // Deux références.
            REQUIRE(ctx.file_service->retain(uuid).get());
            REQUIRE(ctx.file_service->retain(uuid).get());

            // Un release : compteur 2 → 1, donc > 0.
            const bool released = ctx.file_service->release(
                                                      uuid, sea::domain::OnDeleteFile::Cascade).get();
            CHECK(released);

            // Compteur encore > 0 : record et fichier subsistent.
            const auto meta = ctx.file_repo->find_by_id(uuid).get();
            REQUIRE(meta.has_value());
            CHECK(meta->reference_count == 1);
            CHECK(ctx.file_storage->exists(path));

        });
    }

    // ───────────────────────────────────────────────────────────────
    // release en mode SetNull : le compteur est décrémenté, mais le
    // fichier physique est PRÉSERVÉ même à 0 (cleanup délégué à un job
    // offline). On vérifie que le fichier survit.
    // ───────────────────────────────────────────────────────────────
    TEST_CASE("release SetNull préserve le fichier même à compteur 0") {
        sea::itest::run_on_reactor([] {
            FileTestContext ctx;

            const auto result = ctx.file_service->upload(
                                                    open_config(), "kept.bin", "application/octet-stream",
                                                    "kept content").get();
            const std::string uuid = result.uuid;
            const std::string path = result.storage_path;

            REQUIRE(ctx.file_service->retain(uuid).get());

            // release SetNull : compteur 1 → 0, mais fichier conservé.
            const bool released = ctx.file_service->release(
                                                      uuid, sea::domain::OnDeleteFile::SetNull).get();
            CHECK(released);

            // Le fichier physique doit toujours exister malgré le 0.
            CHECK(ctx.file_storage->exists(path));

        });
    }

} // TEST_SUITE