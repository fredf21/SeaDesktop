#include "test_domain_models.h"

#include "database_config.h"
#include "entity.h"
#include "field.h"
#include "field_type.h"
#include "file_field_config.h"
#include "file_metadata.h"
#include "pagination.h"
#include "project.h"
#include "relation.h"
#include "schema.h"
#include "service.h"
#include "storage_config.h"

#include <QtTest>

#include <cstdint>
#include <string>
#include <vector>

using namespace sea::domain;

namespace {

QString qs(std::string_view value)
{
    return QString::fromStdString(std::string(value));
}

Field makeTestField(const std::string& name, FieldType type)
{
    Field f{};
    f.name = name;
    f.type = type;
    return f;
}

Entity makeTestEntity(const std::string& name)
{
    Entity e{};
    e.name = name;
    return e;
}

Entity makeEntityWithField(const std::string& entityName,
                           const std::string& fieldName,
                           FieldType type)
{
    Entity e{};
    e.name = entityName;
    e.fields.push_back(makeTestField(fieldName, type));
    return e;
}

Service makeService(const std::string& name, DatabaseType dbType)
{
    Service s{};
    s.name = name;
    s.database_config.type = dbType;
    return s;
}

} // namespace

// ─────────────────────────────────────────────
// DatabaseConfig
// ─────────────────────────────────────────────

void TestDomainModels::databaseConfig_defaultShouldUseMemory()
{
    DatabaseConfig cfg{};

    QVERIFY(cfg.is_memory());
    QVERIFY(!cfg.is_mysql());
    QVERIFY(!cfg.is_postgres());
    QVERIFY(!cfg.is_mongo());
    QVERIFY(!cfg.requires_network_connection());
    QVERIFY(cfg.has_connection_info());

    QCOMPARE(qs(to_string(cfg.type)), QString("memory"));
}

void TestDomainModels::databaseConfig_mysqlShouldRequireNetworkConnection()
{
    DatabaseConfig cfg{};
    cfg.type = DatabaseType::MySQL;
    cfg.host = "127.0.0.1";
    cfg.port = 3306;
    cfg.database_name = "testdb";

    QVERIFY(cfg.is_mysql());
    QVERIFY(cfg.requires_network_connection());
    QVERIFY(cfg.has_connection_info());

    QCOMPARE(qs(to_string(cfg.type)), QString("mysql"));
}

void TestDomainModels::databaseConfig_externalDbWithoutConnectionInfoShouldBeInvalid()
{
    DatabaseConfig cfg{};
    cfg.type = DatabaseType::PostgreSQL;
    cfg.host = "";
    cfg.port = 5432;
    cfg.database_name = "app";

    QVERIFY(cfg.requires_network_connection());
    QVERIFY(!cfg.has_connection_info());
}

void TestDomainModels::databaseConfig_migrationModeFromStringShouldBeCaseInsensitive()
{
    QCOMPARE(migration_mode_from_string("conservative").value(),
             MigrationMode::Conservative);

    QCOMPARE(migration_mode_from_string("MODIFIED").value(),
             MigrationMode::Modified);

    QCOMPARE(migration_mode_from_string("Aggressive").value(),
             MigrationMode::Aggressive);

    QVERIFY(!migration_mode_from_string("danger").has_value());

    QCOMPARE(qs(to_string(MigrationMode::Conservative)), QString("conservative"));
    QCOMPARE(qs(to_string(MigrationMode::Modified)), QString("modified"));
    QCOMPARE(qs(to_string(MigrationMode::Aggressive)), QString("aggressive"));
}

void TestDomainModels::databaseConfig_seedsModeFromStringShouldBeCaseInsensitive()
{
    QCOMPARE(seeds_mode_from_string("once").value(), SeedsMode::Once);
    QCOMPARE(seeds_mode_from_string("ALWAYS").value(), SeedsMode::Always);
    QVERIFY(!seeds_mode_from_string("sometimes").has_value());

    QCOMPARE(qs(to_string(SeedsMode::Once)), QString("once"));
    QCOMPARE(qs(to_string(SeedsMode::Always)), QString("always"));
    QCOMPARE(qs(to_string(SeedsErrorPolicy::Continue)), QString("continue"));
    QCOMPARE(qs(to_string(SeedsErrorPolicy::Abort)), QString("abort"));
}

// ─────────────────────────────────────────────
// FieldType
// ─────────────────────────────────────────────

void TestDomainModels::fieldType_toStringShouldReturnExpectedValues()
{
    QCOMPARE(qs(to_string(FieldType::String)), QString("string"));
    QCOMPARE(qs(to_string(FieldType::Int)), QString("int"));
    QCOMPARE(qs(to_string(FieldType::Float)), QString("float"));
    QCOMPARE(qs(to_string(FieldType::Bool)), QString("bool"));
    QCOMPARE(qs(to_string(FieldType::Timestamp)), QString("timestamp"));
    QCOMPARE(qs(to_string(FieldType::UUID)), QString("uuid"));
    QCOMPARE(qs(to_string(FieldType::Password)), QString("password"));
    QCOMPARE(qs(to_string(FieldType::Email)), QString("email"));
    QCOMPARE(qs(to_string(FieldType::Text)), QString("text"));
    QCOMPARE(qs(to_string(FieldType::BigInt)), QString("bigint"));
    QCOMPARE(qs(to_string(FieldType::SmallInt)), QString("smallint"));
    QCOMPARE(qs(to_string(FieldType::Decimal)), QString("decimal"));
    QCOMPARE(qs(to_string(FieldType::Json)), QString("json"));
    QCOMPARE(qs(to_string(FieldType::Binary)), QString("binary"));
    QCOMPARE(qs(to_string(FieldType::File)), QString("file"));
    QCOMPARE(qs(to_string(FieldType::Native)), QString("native"));
}

void TestDomainModels::fieldType_fromStringShouldBeCaseInsensitive()
{
    QCOMPARE(field_type_from_string("string").value(), FieldType::String);
    QCOMPARE(field_type_from_string("STRING").value(), FieldType::String);
    QCOMPARE(field_type_from_string("Int").value(), FieldType::Int);
    QCOMPARE(field_type_from_string("FILE").value(), FieldType::File);

    QVERIFY(!field_type_from_string("unknown_type").has_value());

    // Note : dans ton code actuel, "native" n'est pas parsé depuis string.
    QVERIFY(!field_type_from_string("native").has_value());
}

void TestDomainModels::fieldType_helpersShouldClassifyTypes()
{
    QVERIFY(is_logical_type(FieldType::Password));
    QVERIFY(is_logical_type(FieldType::Email));
    QVERIFY(is_logical_type(FieldType::File));
    QVERIFY(is_logical_type(FieldType::Native));
    QVERIFY(!is_logical_type(FieldType::String));

    QVERIFY(is_numeric(FieldType::SmallInt));
    QVERIFY(is_numeric(FieldType::Int));
    QVERIFY(is_numeric(FieldType::BigInt));
    QVERIFY(is_numeric(FieldType::Float));
    QVERIFY(is_numeric(FieldType::Decimal));
    QVERIFY(!is_numeric(FieldType::String));

    QVERIFY(is_boolean(FieldType::Bool));
    QVERIFY(!is_boolean(FieldType::Int));

    QVERIFY(is_file(FieldType::File));
    QVERIFY(!is_file(FieldType::String));
}

// ─────────────────────────────────────────────
// Field
// ─────────────────────────────────────────────

void TestDomainModels::field_makeFieldShouldInitializeNameAndType()
{
    Field f = make_field("email", FieldType::Email);

    QCOMPARE(QString::fromStdString(f.name), QString("email"));
    QCOMPARE(f.type, FieldType::Email);
    QVERIFY(f.required);
    QVERIFY(f.serializable);
    QVERIFY(!f.has_default());
}

void TestDomainModels::field_defaultValueShouldSetHasDefault()
{
    Field f = make_field("age", FieldType::Int);

    QVERIFY(!f.has_default());

    default_value(f, std::int64_t(42));

    QVERIFY(f.has_default());
    QVERIFY(std::holds_alternative<std::int64_t>(f.default_val));
    QCOMPARE(std::get<std::int64_t>(f.default_val), std::int64_t(42));
}

void TestDomainModels::field_hiddenShouldDisableSerialization()
{
    Field f = make_field("password", FieldType::Password);

    QVERIFY(f.serializable);

    hidden(f);

    QVERIFY(!f.serializable);
}

void TestDomainModels::field_renamedFromShouldSetPreviousName()
{
    Field f = make_field("phone_number", FieldType::String);

    QVERIFY(!f.has_previous_name());

    renamed_from(f, "phone");

    QVERIFY(f.has_previous_name());
    QCOMPARE(QString::fromStdString(*f.previous_name), QString("phone"));
}

void TestDomainModels::field_asFileShouldConfigureFileField()
{
    Field f = make_field("avatar", FieldType::String);

    FileFieldConfig cfg{};
    cfg.storage_path = "users/avatars";
    cfg.max_size_bytes = 1024;

    as_file(f, cfg);

    QCOMPARE(f.type, FieldType::File);
    QVERIFY(f.file_config.has_value());
    QVERIFY(f.is_file_field());
    QCOMPARE(QString::fromStdString(f.file_config->storage_path),
             QString("users/avatars"));
}

// ─────────────────────────────────────────────
// FileFieldConfig
// ─────────────────────────────────────────────

void TestDomainModels::fileFieldConfig_defaultShouldAcceptEverything()
{
    FileFieldConfig cfg{};

    QVERIFY(!cfg.has_size_limit());
    QVERIFY(!cfg.has_mime_filter());
    QVERIFY(!cfg.has_extension_filter());
    QVERIFY(!cfg.has_storage_path());

    QVERIFY(cfg.accepts_mime("image/png"));
    QVERIFY(cfg.accepts_mime("application/pdf"));

    QVERIFY(cfg.accepts_extension(".png"));
    QVERIFY(cfg.accepts_extension("pdf"));

    QVERIFY(cfg.accepts_size(0));
    QVERIFY(cfg.accepts_size(999999));
}

void TestDomainModels::fileFieldConfig_mimeWhitelistShouldFilterValues()
{
    FileFieldConfig cfg{};
    cfg.allowed_mime_types = { "image/png", "image/jpeg" };

    QVERIFY(cfg.has_mime_filter());

    QVERIFY(cfg.accepts_mime("image/png"));
    QVERIFY(cfg.accepts_mime("image/jpeg"));
    QVERIFY(!cfg.accepts_mime("application/pdf"));
}

void TestDomainModels::fileFieldConfig_extensionWhitelistShouldBeCaseInsensitive()
{
    FileFieldConfig cfg{};
    cfg.allowed_extensions = { ".png", ".jpg" };

    QVERIFY(cfg.has_extension_filter());

    QVERIFY(cfg.accepts_extension(".png"));
    QVERIFY(cfg.accepts_extension("png"));
    QVERIFY(cfg.accepts_extension(".PNG"));
    QVERIFY(cfg.accepts_extension("JPG"));

    QVERIFY(!cfg.accepts_extension(".pdf"));
}

void TestDomainModels::fileFieldConfig_sizeLimitShouldRejectTooLargeFile()
{
    FileFieldConfig cfg{};
    cfg.max_size_bytes = std::size_t(1024);

    QVERIFY(cfg.has_size_limit());

    QVERIFY(cfg.accepts_size(0));
    QVERIFY(cfg.accepts_size(1024));
    QVERIFY(!cfg.accepts_size(1025));
}

void TestDomainModels::fileFieldConfig_onDeleteFromStringShouldParseValidValues()
{
    QCOMPARE(on_delete_file_from_string("cascade").value(),
             OnDeleteFile::Cascade);

    QCOMPARE(on_delete_file_from_string("SET_NULL").value(),
             OnDeleteFile::SetNull);

    QCOMPARE(on_delete_file_from_string("Restrict").value(),
             OnDeleteFile::Restrict);

    QVERIFY(!on_delete_file_from_string("delete").has_value());

    QCOMPARE(qs(to_string(OnDeleteFile::Cascade)), QString("cascade"));
    QCOMPARE(qs(to_string(OnDeleteFile::SetNull)), QString("set_null"));
    QCOMPARE(qs(to_string(OnDeleteFile::Restrict)), QString("restrict"));
}

// ─────────────────────────────────────────────
// FileMetadata
// ─────────────────────────────────────────────

void TestDomainModels::fileMetadata_defaultReferenceCountShouldBeOrphan()
{
    FileMetadata metadata{};

    QCOMPARE(metadata.reference_count, std::int32_t(0));
    QVERIFY(metadata.is_orphan());
}

void TestDomainModels::fileMetadata_positiveReferenceCountShouldNotBeOrphan()
{
    FileMetadata metadata{};
    metadata.reference_count = 2;

    QVERIFY(!metadata.is_orphan());

    metadata.reference_count = -1;
    QVERIFY(metadata.is_orphan());
}

// ─────────────────────────────────────────────
// Pagination
// ─────────────────────────────────────────────

void TestDomainModels::paginationConfig_emptyShouldHaveNoMode()
{
    PaginationConfig cfg{};

    QVERIFY(!cfg.has_page());
    QVERIFY(!cfg.has_offset());
    QVERIFY(!cfg.has_cursor());
    QVERIFY(!cfg.any());
}

void TestDomainModels::paginationConfig_pageModeShouldBeDetected()
{
    PaginationConfig cfg{};
    cfg.page = PagePagination{};

    QVERIFY(cfg.has_page());
    QVERIFY(!cfg.has_offset());
    QVERIFY(!cfg.has_cursor());
    QVERIFY(cfg.any());
}

void TestDomainModels::paginationConfig_multipleModesShouldBeDetected()
{
    PaginationConfig cfg{};
    cfg.page = PagePagination{};
    cfg.offset = OffsetPagination{};
    cfg.cursor = CursorPagination{};

    QVERIFY(cfg.has_page());
    QVERIFY(cfg.has_offset());
    QVERIFY(cfg.has_cursor());
    QVERIFY(cfg.any());
}

// ─────────────────────────────────────────────
// Relation
// ─────────────────────────────────────────────

void TestDomainModels::relation_toStringShouldReturnExpectedValues()
{
    QCOMPARE(qs(to_string(RelationKind::BelongsTo)), QString("belongs_to"));
    QCOMPARE(qs(to_string(RelationKind::HasMany)), QString("has_many"));
    QCOMPARE(qs(to_string(RelationKind::HasOne)), QString("has_one"));
    QCOMPARE(qs(to_string(RelationKind::ManyToMany)), QString("many_to_many"));

    QCOMPARE(qs(to_string(OnDelete::Cascade)), QString("cascade"));
    QCOMPARE(qs(to_string(OnDelete::SetNull)), QString("set_null"));
    QCOMPARE(qs(to_string(OnDelete::Restrict)), QString("restrict"));
}

void TestDomainModels::relation_helpersShouldMatchRelationKind()
{
    Relation belongsTo{};
    belongsTo.kind = RelationKind::BelongsTo;

    QVERIFY(belongsTo.uses_local_foreign_key());
    QVERIFY(!belongsTo.uses_target_foreign_key());
    QVERIFY(!belongsTo.uses_pivot_table());
    QVERIFY(!belongsTo.is_to_many());

    Relation hasMany{};
    hasMany.kind = RelationKind::HasMany;

    QVERIFY(!hasMany.uses_local_foreign_key());
    QVERIFY(hasMany.uses_target_foreign_key());
    QVERIFY(!hasMany.uses_pivot_table());
    QVERIFY(hasMany.is_to_many());

    Relation manyToMany{};
    manyToMany.kind = RelationKind::ManyToMany;

    QVERIFY(!manyToMany.uses_local_foreign_key());
    QVERIFY(!manyToMany.uses_target_foreign_key());
    QVERIFY(manyToMany.uses_pivot_table());
    QVERIFY(manyToMany.is_to_many());
}

// ─────────────────────────────────────────────
// Entity
// ─────────────────────────────────────────────

void TestDomainModels::entity_routePrefixShouldPluralizeSimpleNames()
{
    Entity user{};
    user.name = "User";

    QCOMPARE(QString::fromStdString(user.route_prefix()), QString("/users"));

    Entity category{};
    category.name = "Toy";

    QCOMPARE(QString::fromStdString(category.route_prefix()), QString("/toys"));
}

void TestDomainModels::entity_routePrefixShouldConvertYToIes()
{
    Entity category{};
    category.name = "Category";

    QCOMPARE(QString::fromStdString(category.route_prefix()),
             QString("/categories"));

    Entity company{};
    company.name = "Company";

    QCOMPARE(QString::fromStdString(company.route_prefix()),
             QString("/companies"));
}

void TestDomainModels::entity_findFieldShouldReturnExistingField()
{
    Entity entity = makeTestEntity("User");
    entity.fields.push_back(makeTestField("id", FieldType::UUID));
    entity.fields.push_back(makeTestField("email", FieldType::Email));

    QVERIFY(entity.has_field("email"));
    QVERIFY(entity.find_field("email") != nullptr);
    QVERIFY(entity.find_field("missing") == nullptr);
}

void TestDomainModels::entity_serializableFieldsShouldExcludeHiddenFields()
{
    Entity entity = makeTestEntity("User");

    Field email = makeTestField("email", FieldType::Email);
    Field password = makeTestField("password", FieldType::Password);
    password.serializable = false;

    entity.fields = { email, password };

    const auto fields = entity.serializable_fields();

    QCOMPARE(fields.size(), std::size_t(1));
    QCOMPARE(QString::fromStdString(fields[0].name), QString("email"));
}

void TestDomainModels::entity_findRelationShouldReturnExistingRelation()
{
    Entity entity = makeTestEntity("User");

    Relation relation{};
    relation.name = "posts";
    relation.target_entity = "Post";
    relation.kind = RelationKind::HasMany;

    entity.relations.push_back(relation);

    QVERIFY(entity.has_relation("posts"));
    QVERIFY(entity.find_relation("posts") != nullptr);
    QVERIFY(entity.find_relation("roles") == nullptr);
}

void TestDomainModels::entity_paginationHelpersShouldReflectConfig()
{
    Entity entity = makeTestEntity("User");

    QVERIFY(!entity.has_pagination());
    QVERIFY(!entity.has_page_pagination());

    PaginationConfig pagination{};
    pagination.page = PagePagination{};

    entity.pagination = pagination;

    QVERIFY(entity.has_pagination());
    QVERIFY(entity.has_page_pagination());
    QVERIFY(!entity.has_offset_pagination());
    QVERIFY(!entity.has_cursor_pagination());
}

void TestDomainModels::entity_hasFileFieldsShouldDetectConfiguredFileField()
{
    Entity entity = makeTestEntity("User");

    Field name = makeTestField("name", FieldType::String);

    Field avatar = makeTestField("avatar", FieldType::File);
    FileFieldConfig cfg{};
    cfg.storage_path = "users/avatars";
    avatar.file_config = cfg;

    entity.fields = { name, avatar };

    QVERIFY(entity.has_file_fields());
}

void TestDomainModels::entity_seedRecordShouldDetectAlias()
{
    SeedRecord seed{};

    QVERIFY(!seed.has_alias());

    seed.alias = "admin_user";

    QVERIFY(seed.has_alias());
}

// ─────────────────────────────────────────────
// Schema
// ─────────────────────────────────────────────

void TestDomainModels::schema_emptyShouldBeTrueWhenNoEntities()
{
    Schema schema{};

    QVERIFY(schema.empty());
    QVERIFY(!schema.has_entity("User"));
    QVERIFY(schema.find_entity("User") == nullptr);
}

void TestDomainModels::schema_findEntityShouldReturnExistingEntity()
{
    Schema schema{};
    schema.entities.push_back(makeEntityWithField("User", "id", FieldType::UUID));
    schema.entities.push_back(makeEntityWithField("Role", "id", FieldType::UUID));

    QVERIFY(!schema.empty());
    QVERIFY(schema.has_entity("User"));
    QVERIFY(schema.find_entity("User") != nullptr);
    QVERIFY(schema.find_entity("Missing") == nullptr);
}

void TestDomainModels::schema_crudEntitiesShouldReturnOnlyCrudEnabledEntities()
{
    Entity user = makeEntityWithField("User", "id", FieldType::UUID);
    user.options.enable_crud = true;

    Entity audit = makeEntityWithField("AuditLog", "id", FieldType::UUID);
    audit.options.enable_crud = false;

    Schema schema{};
    schema.entities = { user, audit };

    const auto crud = schema.crud_entities();

    QCOMPARE(crud.size(), std::size_t(1));
    QCOMPARE(QString::fromStdString(crud[0]->name), QString("User"));
}

void TestDomainModels::schema_authEntitiesShouldReturnOnlyAuthSources()
{
    Entity user = makeEntityWithField("User", "id", FieldType::UUID);
    user.options.is_auth_source = true;

    Entity product = makeEntityWithField("Product", "id", FieldType::UUID);
    product.options.is_auth_source = false;

    Schema schema{};
    schema.entities = { user, product };

    const auto auth = schema.auth_entities();

    QCOMPARE(auth.size(), std::size_t(1));
    QCOMPARE(QString::fromStdString(auth[0]->name), QString("User"));
}

void TestDomainModels::schema_hasFileFieldsShouldDetectFileFields()
{
    Entity user = makeTestEntity("User");

    Field avatar = makeTestField("avatar", FieldType::File);
    FileFieldConfig cfg{};
    cfg.storage_path = "users/avatars";
    avatar.file_config = cfg;

    user.fields.push_back(avatar);

    Schema schema{};
    schema.entities.push_back(user);

    QVERIFY(schema.has_file_fields());
}

// ─────────────────────────────────────────────
// Service
// ─────────────────────────────────────────────

void TestDomainModels::service_defaultPortShouldBeValid()
{
    Service service{};

    QCOMPARE(service.port, std::uint16_t(8080));
    QVERIFY(service.has_valid_port());

    service.port = 0;
    QVERIFY(!service.has_valid_port());
}

void TestDomainModels::service_hasEntitiesShouldReflectSchema()
{
    Service service{};

    QVERIFY(!service.has_entities());

    service.schema.entities.push_back(
        makeEntityWithField("User", "id", FieldType::UUID)
        );

    QVERIFY(service.has_entities());
}

void TestDomainModels::service_databaseHelpersShouldReflectDatabaseType()
{
    Service service{};

    service.database_config.type = DatabaseType::Memory;
    QVERIFY(service.uses_memory_database());
    QVERIFY(!service.uses_external_database());

    service.database_config.type = DatabaseType::MySQL;
    QVERIFY(!service.uses_memory_database());
    QVERIFY(service.uses_external_database());
}

void TestDomainModels::service_findEntityShouldDelegateToSchema()
{
    Service service{};
    service.schema.entities.push_back(
        makeEntityWithField("User", "id", FieldType::UUID)
        );

    QVERIFY(service.find_entity("User") != nullptr);
    QVERIFY(service.find_entity("Missing") == nullptr);
}

void TestDomainModels::service_hasFileFieldsShouldDelegateToSchema()
{
    Service service{};

    QVERIFY(!service.has_file_fields());

    Entity user = makeTestEntity("User");

    Field file = makeTestField("avatar", FieldType::File);
    FileFieldConfig cfg{};
    cfg.storage_path = "users/avatars";
    file.file_config = cfg;

    user.fields.push_back(file);
    service.schema.entities.push_back(user);

    QVERIFY(service.has_file_fields());
}

// ─────────────────────────────────────────────
// Project
// ─────────────────────────────────────────────

void TestDomainModels::project_emptyShouldBeTrueWhenNoServices()
{
    Project project{};

    QVERIFY(project.empty());
    QVERIFY(!project.has_service("UserService"));
    QVERIFY(project.find_service("UserService") == nullptr);
}

void TestDomainModels::project_findServiceShouldReturnExistingService()
{
    Project project{};
    project.name = "Demo";

    Service userService{};
    userService.name = "UserService";

    project.services.push_back(userService);

    QVERIFY(!project.empty());
    QVERIFY(project.has_service("UserService"));
    QVERIFY(project.find_service("UserService") != nullptr);
    QVERIFY(project.find_service("MissingService") == nullptr);
}

void TestDomainModels::project_memoryServicesShouldReturnOnlyMemoryServices()
{
    Project project{};

    project.services.push_back(makeService("MemoryService", DatabaseType::Memory));
    project.services.push_back(makeService("MySQLService", DatabaseType::MySQL));

    const auto memory = project.memory_services();

    QCOMPARE(memory.size(), std::size_t(1));
    QCOMPARE(QString::fromStdString(memory[0]->name), QString("MemoryService"));
}

void TestDomainModels::project_externalDbServicesShouldReturnOnlyExternalServices()
{
    Project project{};

    project.services.push_back(makeService("MemoryService", DatabaseType::Memory));
    project.services.push_back(makeService("MySQLService", DatabaseType::MySQL));
    project.services.push_back(makeService("MongoService", DatabaseType::MongoDB));

    const auto external = project.external_db_services();

    QCOMPARE(external.size(), std::size_t(2));
}

// ─────────────────────────────────────────────
// StorageConfig
// ─────────────────────────────────────────────

void TestDomainModels::storageConfig_defaultShouldUseFilesystem()
{
    StorageConfig cfg{};

    QVERIFY(cfg.is_filesystem());
    QVERIFY(!cfg.has_root_path());

    QCOMPARE(cfg.file_mode, std::uint32_t(0640));
    QCOMPARE(cfg.directory_mode, std::uint32_t(0750));
}

void TestDomainModels::storageConfig_rootPathShouldBeDetected()
{
    StorageConfig cfg{};
    cfg.root_path = "/tmp/seadesktop/uploads";

    QVERIFY(cfg.is_filesystem());
    QVERIFY(cfg.has_root_path());
}