#include "test_schema_validator.h"

#include "schema_validator.h"
#include "schema.h"
#include "entity.h"
#include "field.h"
#include "field_type.h"
#include "relation.h"

#include <QtTest>

#include <cstdint>
#include <string>
#include <vector>

using sea::domain::SchemaValidator;
using sea::domain::Schema;
using sea::domain::Entity;
using sea::domain::Field;
using sea::domain::FieldType;
using sea::domain::Relation;
using sea::domain::RelationKind;

namespace {

Field makeField(const std::string& name, FieldType type, bool required = true)
{
    Field f{};
    f.name = name;
    f.type = type;
    f.required = required;
    return f;
}

Entity makeEntity(const std::string& name, std::vector<Field> fields)
{
    Entity e{};
    e.name = name;
    e.fields = std::move(fields);
    return e;
}

Schema makeSchema(std::vector<Entity> entities)
{
    Schema s{};
    s.entities = std::move(entities);
    return s;
}

bool containsError(const std::vector<std::string>& errors, const std::string& text)
{
    for (const auto& error : errors) {
        if (error.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

void TestSchemaValidator::validate_emptySchema_shouldReportError()
{
    SchemaValidator validator;
    Schema schema;

    const auto errors = validator.validate(schema);

    QCOMPARE(errors.size(), std::size_t(1));
    QVERIFY(containsError(errors, "does not contain any entity"));
}

void TestSchemaValidator::validate_validSimpleSchema_shouldPass()
{
    SchemaValidator validator;

    Entity user = makeEntity("User", {
                                         makeField("id", FieldType::UUID),
                                         makeField("name", FieldType::String),
                                         makeField("age", FieldType::Int)
                                     });

    Schema schema = makeSchema({ user });

    const auto errors = validator.validate(schema);

    QVERIFY(errors.empty());
}

void TestSchemaValidator::validate_emptyEntityName_shouldReportError()
{
    SchemaValidator validator;

    Entity entity = makeEntity("", {
        makeField("name", FieldType::String)
    });

    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);

    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "empty name"));
}

void TestSchemaValidator::validate_invalidEntityName_shouldReportError()
{
    SchemaValidator validator;

    Entity entity = makeEntity("123User", {
        makeField("name", FieldType::String)
    });

    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);

    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "Invalid entity name"));
}

void TestSchemaValidator::validate_duplicateEntityName_shouldReportError()
{
    SchemaValidator validator;

    Entity e1 = makeEntity("User", {
        makeField("name", FieldType::String)
    });

    Entity e2 = makeEntity("User", {
        makeField("email", FieldType::Email)
    });

    Schema schema = makeSchema({ e1, e2 });

    const auto errors = validator.validate(schema);

    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "Duplicate entity name"));
}

void TestSchemaValidator::validate_entityWithoutFields_shouldReportError()
{
    SchemaValidator validator;

    Entity entity{};
    entity.name = "User";

    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);

    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "does not contain any field"));
}

void TestSchemaValidator::validate_emptyFieldName_shouldReportError()
{
    SchemaValidator validator;

    Entity entity = makeEntity("User", {
        makeField("", FieldType::String)
    });

    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);

    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "unnamed field"));
}

void TestSchemaValidator::validate_invalidFieldName_shouldReportError()
{
    SchemaValidator validator;

    Entity entity = makeEntity("User", {
        makeField("first-name", FieldType::String)
    });

    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);

    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "Invalid field name"));
}

void TestSchemaValidator::validate_duplicateFieldName_shouldReportError()
{
    SchemaValidator validator;

    Entity entity = makeEntity("User", {
                                           makeField("name", FieldType::String),
                                           makeField("name", FieldType::Text)
                                       });

    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);

    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "Duplicate field"));
}

void TestSchemaValidator::validate_passwordSerializable_shouldReportError()
{
    SchemaValidator validator;

    Field password = makeField("password", FieldType::Password);
    password.serializable = true;

    Entity entity = makeEntity("User", { password });
    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);

    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "should not be serializable"));
}

void TestSchemaValidator::validate_passwordWithDefault_shouldReportError()
{
    SchemaValidator validator;

    Field password = makeField("password", FieldType::Password);
    password.default_val = std::string("secret");

    Entity entity = makeEntity("User", { password });
    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);

    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "cannot have a default value"));
}

void TestSchemaValidator::validate_binaryWithDefault_shouldReportError()
{
    SchemaValidator validator;

    Field data = makeField("data", FieldType::Binary);
    data.default_val = std::string("abc");

    Entity entity = makeEntity("FileRecord", { data });
    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);

    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "type binary"));
}

void TestSchemaValidator::validate_maxLengthOnString_shouldPass()
{
    SchemaValidator validator;

    Field name = makeField("name", FieldType::String);
    name.max_length = std::size_t(100);

    Entity entity = makeEntity("User", { name });
    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);

    QVERIFY(errors.empty());
}

void TestSchemaValidator::validate_maxLengthOnInt_shouldReportError()
{
    SchemaValidator validator;

    Field age = makeField("age", FieldType::Int);
    age.max_length = std::size_t(10);

    Entity entity = makeEntity("User", { age });
    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);

    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "max_length with an incompatible type"));
}

void TestSchemaValidator::validate_maxLengthZero_shouldReportError()
{
    SchemaValidator validator;

    Field name = makeField("name", FieldType::String);
    name.max_length = std::size_t(0);

    Entity entity = makeEntity("User", { name });
    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);

    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "max_length = 0"));
}

void TestSchemaValidator::validate_unsignedValueOnInt_shouldPass()
{
    SchemaValidator validator;

    Field age = makeField("age", FieldType::Int);
    age.unsigned_value = true;

    Entity entity = makeEntity("User", { age });
    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);

    QVERIFY(errors.empty());
}

void TestSchemaValidator::validate_unsignedValueOnString_shouldReportError()
{
    SchemaValidator validator;

    Field name = makeField("name", FieldType::String);
    name.unsigned_value = true;

    Entity entity = makeEntity("User", { name });
    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);

    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "unsigned_value is valid"));
}

void TestSchemaValidator::validate_minGreaterThanMax_shouldReportError()
{
    SchemaValidator validator;

    Field age = makeField("age", FieldType::Int);
    age.min_value = std::int64_t(100);
    age.max_value = std::int64_t(10);

    Entity entity = makeEntity("User", { age });
    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);

    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "min_value > max_value"));
}

void TestSchemaValidator::validate_relationUnknownTarget_shouldReportError()
{
    SchemaValidator validator;

    Entity user = makeEntity("User", {
        makeField("id", FieldType::UUID)
    });

    Relation relation{};
    relation.name = "posts";
    relation.target_entity = "Post";
    relation.kind = RelationKind::HasMany;

    user.relations.push_back(relation);

    Schema schema = makeSchema({ user });

    const auto errors = validator.validate(schema);

    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "targets an unknown entity"));
}

void TestSchemaValidator::validate_manyToManyWithoutPivotTable_shouldReportError()
{
    SchemaValidator validator;

    Entity user = makeEntity("User", {
        makeField("id", FieldType::UUID)
    });

    Entity role = makeEntity("Role", {
        makeField("id", FieldType::UUID)
    });

    Relation relation{};
    relation.name = "roles";
    relation.target_entity = "Role";
    relation.kind = RelationKind::ManyToMany;
    relation.pivot_table = "";

    user.relations.push_back(relation);

    Schema schema = makeSchema({ user, role });

    const auto errors = validator.validate(schema);

    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "must define a pivot_table"));
}

void TestSchemaValidator::validate_belongsToWithoutFkColumn_shouldReportError()
{
    SchemaValidator validator;

    Entity author = makeEntity("Author", {
        makeField("id", FieldType::UUID)
    });

    Entity article = makeEntity("Article", {
        makeField("id", FieldType::UUID)
    });

    Relation relation{};
    relation.name = "author";
    relation.target_entity = "Author";
    relation.kind = RelationKind::BelongsTo;
    relation.fk_column = "";

    article.relations.push_back(relation);

    Schema schema = makeSchema({ author, article });

    const auto errors = validator.validate(schema);

    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "must define an fk_column"));
}

void TestSchemaValidator::validate_fileWithoutConfig_shouldReportError()
{
    SchemaValidator validator;

    Field file = makeField("avatar", FieldType::File);
    file.file_config = std::nullopt;

    Entity entity = makeEntity("User", { file });
    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);

    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "has no file_config"));
}

void TestSchemaValidator::validate_fileWithValidConfig_shouldPass()
{
    SchemaValidator validator;

    Field file = makeField("avatar", FieldType::File);

    sea::domain::FileFieldConfig cfg{};
    cfg.storage_path = "users/avatars";
    cfg.max_size_bytes = std::size_t(5 * 1024 * 1024);
    cfg.allowed_mime_types = { "image/png", "image/jpeg" };
    cfg.allowed_extensions = { ".png", ".jpg", ".jpeg" };
    cfg.on_delete = sea::domain::OnDeleteFile::Cascade;

    file.file_config = cfg;

    Entity entity = makeEntity("User", { file });
    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);

    QVERIFY(errors.empty());
}

void TestSchemaValidator::validate_fileWithAbsoluteStoragePath_shouldReportError()
{
    SchemaValidator validator;

    Field file = makeField("avatar", FieldType::File);

    sea::domain::FileFieldConfig cfg{};
    cfg.storage_path = "/users/avatars";

    file.file_config = cfg;

    Entity entity = makeEntity("User", { file });
    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);

    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "absolute storage_path"));
}

void TestSchemaValidator::validate_fileWithPathTraversal_shouldReportError()
{
    SchemaValidator validator;

    Field file = makeField("avatar", FieldType::File);

    sea::domain::FileFieldConfig cfg{};
    cfg.storage_path = "users/../avatars";

    file.file_config = cfg;

    Entity entity = makeEntity("User", { file });
    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);

    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "path traversal"));
}

void TestSchemaValidator::validate_fileWithDuplicateStoragePath_shouldReportError()
{
    SchemaValidator validator;

    sea::domain::FileFieldConfig cfg{};
    cfg.storage_path = "uploads/shared";

    Field avatar = makeField("avatar", FieldType::File);
    avatar.file_config = cfg;

    Field document = makeField("document", FieldType::File);
    document.file_config = cfg;

    Entity entity = makeEntity("User", { avatar, document });
    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);

    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "partage son storage_path"));
}

void TestSchemaValidator::validate_paginationWithoutMode_shouldReportError()
{
    SchemaValidator validator;

    Entity entity = makeEntity("User", {
                                           makeField("id", FieldType::UUID),
                                           makeField("name", FieldType::String)
                                       });

    sea::domain::PaginationConfig pagination{};
    entity.pagination = pagination;

    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);

    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "does not enable any mode"));
}

void TestSchemaValidator::validate_pagePaginationDefaultGreaterThanMax_shouldReportError()
{
    SchemaValidator validator;

    Entity entity = makeEntity("User", {
                                           makeField("id", FieldType::UUID),
                                           makeField("name", FieldType::String)
                                       });

    sea::domain::PagePagination page{};
    page.default_page_size = 100;
    page.max_page_size = 50;
    page.sortable_fields = { "name" };

    sea::domain::PaginationConfig pagination{};
    pagination.page = page;

    entity.pagination = pagination;

    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);

    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "default_page_size > max_page_size"));
}

void TestSchemaValidator::validate_pagePaginationUnknownSortableField_shouldReportError()
{
    SchemaValidator validator;

    Entity entity = makeEntity("User", {
                                           makeField("id", FieldType::UUID),
                                           makeField("name", FieldType::String)
                                       });

    sea::domain::PagePagination page{};
    page.default_page_size = 20;
    page.max_page_size = 100;
    page.sortable_fields = { "unknown_field" };

    sea::domain::PaginationConfig pagination{};
    pagination.page = page;

    entity.pagination = pagination;

    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);

    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "unknown sortable_field"));
}

void TestSchemaValidator::validate_pagePaginationMalformedDefaultSort_shouldReportError()
{
    SchemaValidator validator;

    Entity entity = makeEntity("User", {
                                           makeField("id", FieldType::UUID),
                                           makeField("name", FieldType::String)
                                       });

    sea::domain::PagePagination page{};
    page.default_page_size = 20;
    page.max_page_size = 100;
    page.sortable_fields = { "name" };
    page.default_sort = std::string("name:wrong");

    sea::domain::PaginationConfig pagination{};
    pagination.page = page;

    entity.pagination = pagination;

    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);

    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "malformed default_sort"));
}

void TestSchemaValidator::validate_cursorPaginationUnknownCursorField_shouldReportError()
{
    SchemaValidator validator;

    Entity entity = makeEntity("User", {
                                           makeField("id", FieldType::UUID),
                                           makeField("name", FieldType::String)
                                       });

    sea::domain::CursorPagination cursor{};
    cursor.default_limit = 20;
    cursor.max_limit = 100;
    cursor.cursor_field = "created_at";
    cursor.sort = "name:asc";

    sea::domain::PaginationConfig pagination{};
    pagination.cursor = cursor;

    entity.pagination = pagination;

    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);

    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "unknown cursor_field"));
}

void TestSchemaValidator::validate_pagePaginationValidConfig_shouldPass()
{
    SchemaValidator validator;

    Entity entity = makeEntity("User", {
                                           makeField("id", FieldType::UUID),
                                           makeField("name", FieldType::String),
                                           makeField("created_at", FieldType::Timestamp)
                                       });

    sea::domain::PagePagination page{};
    page.default_page_size = 20;
    page.max_page_size = 100;
    page.sortable_fields = { "name", "created_at" };
    page.default_sort = std::string("created_at:desc");

    sea::domain::PaginationConfig pagination{};
    pagination.page = page;

    entity.pagination = pagination;

    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);

    QVERIFY(errors.empty());
}

void TestSchemaValidator::validate_pagePaginationDefaultSizeZero_shouldReportError()
{
    SchemaValidator validator;

    Entity entity = makeEntity("User", {
                                           makeField("id", FieldType::UUID),
                                           makeField("name", FieldType::String)
                                       });

    sea::domain::PagePagination page{};
    page.default_page_size = 0;
    page.max_page_size = 100;

    sea::domain::PaginationConfig pagination{};
    pagination.page = page;

    entity.pagination = pagination;

    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);

    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "default_page_size = 0"));
}

void TestSchemaValidator::validate_offsetPaginationValidConfig_shouldPass()
{
    SchemaValidator validator;

    Entity entity = makeEntity("User", {
                                           makeField("id", FieldType::UUID),
                                           makeField("name", FieldType::String),
                                           makeField("created_at", FieldType::Timestamp)
                                       });

    sea::domain::OffsetPagination offset{};
    offset.default_limit = 20;
    offset.max_limit = 100;
    offset.sortable_fields = { "name", "created_at" };
    offset.default_sort = std::string("name:asc");

    sea::domain::PaginationConfig pagination{};
    pagination.offset = offset;

    entity.pagination = pagination;

    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);

    QVERIFY(errors.empty());
}

void TestSchemaValidator::validate_offsetPaginationDefaultGreaterThanMax_shouldReportError()
{
    SchemaValidator validator;

    Entity entity = makeEntity("User", {
                                           makeField("id", FieldType::UUID),
                                           makeField("name", FieldType::String)
                                       });

    sea::domain::OffsetPagination offset{};
    offset.default_limit = 200;
    offset.max_limit = 100;
    offset.sortable_fields = { "name" };

    sea::domain::PaginationConfig pagination{};
    pagination.offset = offset;

    entity.pagination = pagination;

    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);

    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "default_limit > max_limit"));
}

void TestSchemaValidator::validate_cursorPaginationValidConfig_shouldPass()
{
    SchemaValidator validator;

    Entity entity = makeEntity("User", {
                                           makeField("id", FieldType::UUID),
                                           makeField("name", FieldType::String),
                                           makeField("created_at", FieldType::Timestamp)
                                       });

    sea::domain::CursorPagination cursor{};
    cursor.default_limit = 20;
    cursor.max_limit = 100;
    cursor.cursor_field = "id";
    cursor.sort = "id:asc";

    sea::domain::PaginationConfig pagination{};
    pagination.cursor = cursor;

    entity.pagination = pagination;

    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);

    QVERIFY(errors.empty());
}

void TestSchemaValidator::validate_cursorPaginationMissingSort_shouldReportError()
{
    SchemaValidator validator;

    Entity entity = makeEntity("User", {
                                           makeField("id", FieldType::UUID),
                                           makeField("name", FieldType::String)
                                       });

    sea::domain::CursorPagination cursor{};
    cursor.default_limit = 20;
    cursor.max_limit = 100;
    cursor.cursor_field = "id";
    cursor.sort = "";

    sea::domain::PaginationConfig pagination{};
    pagination.cursor = cursor;

    entity.pagination = pagination;

    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);

    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "has no 'sort'"));
}
// ═════════════════════════════════════════════════════════════
// COUVERTURE ADDITIONNELLE
// ═════════════════════════════════════════════════════════════

// ── 2. Champs ─────────────────────────────────────────────────

void TestSchemaValidator::validate_maxLengthOnText_shouldPass()
{
    SchemaValidator validator;

    Field bio = makeField("bio", FieldType::Text);
    bio.max_length = std::size_t(500);

    Schema schema = makeSchema({ makeEntity("User", { bio }) });

    const auto errors = validator.validate(schema);
    QVERIFY(errors.empty());
}

void TestSchemaValidator::validate_unsignedValueOnFloat_shouldPass()
{
    // unsigned_value est accepté sur Float (et int/bigint/smallint/decimal).
    SchemaValidator validator;

    Field rating = makeField("rating", FieldType::Float);
    rating.unsigned_value = true;

    Schema schema = makeSchema({ makeEntity("Product", { rating }) });

    const auto errors = validator.validate(schema);
    QVERIFY(errors.empty());
}

void TestSchemaValidator::validate_minGreaterThanMaxDouble_shouldReportError()
{
    // Le contrôle min > max fonctionne aussi pour les bornes double.
    SchemaValidator validator;

    Field rating = makeField("rating", FieldType::Float);
    rating.min_value = double(5.0);
    rating.max_value = double(1.0);

    Schema schema = makeSchema({ makeEntity("Product", { rating }) });

    const auto errors = validator.validate(schema);
    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "min_value > max_value"));
}

void TestSchemaValidator::validate_minEqualsMax_shouldPass()
{
    // min == max est toléré (la contrainte est min > max, stricte).
    SchemaValidator validator;

    Field qty = makeField("quantity", FieldType::Int);
    qty.min_value = std::int64_t(10);
    qty.max_value = std::int64_t(10);

    Schema schema = makeSchema({ makeEntity("Product", { qty }) });

    const auto errors = validator.validate(schema);
    QVERIFY(errors.empty());
}

// ── 3. Champs File ────────────────────────────────────────────

void TestSchemaValidator::validate_fileWithEmptyStoragePath_shouldReportError()
{
    SchemaValidator validator;

    Field file = makeField("avatar", FieldType::File);
    sea::domain::FileFieldConfig cfg{};
    cfg.storage_path = "";
    file.file_config = cfg;

    Schema schema = makeSchema({ makeEntity("User", { file }) });

    const auto errors = validator.validate(schema);
    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "must define a storage_path"));
}

void TestSchemaValidator::validate_fileWithEmptyPathSegment_shouldReportError()
{
    // Un '//' interne dans le storage_path est rejeté.
    SchemaValidator validator;

    Field file = makeField("avatar", FieldType::File);
    sea::domain::FileFieldConfig cfg{};
    cfg.storage_path = "users//avatars";
    file.file_config = cfg;

    Schema schema = makeSchema({ makeEntity("User", { file }) });

    const auto errors = validator.validate(schema);
    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "empty segment"));
}

void TestSchemaValidator::validate_fileUnique_shouldReportError()
{
    // Un champ File ne peut pas être 'unique'.
    SchemaValidator validator;

    Field file = makeField("avatar", FieldType::File);
    file.unique = true;
    sea::domain::FileFieldConfig cfg{};
    cfg.storage_path = "users/avatars";
    file.file_config = cfg;

    Schema schema = makeSchema({ makeEntity("User", { file }) });

    const auto errors = validator.validate(schema);
    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "cannot be 'unique'"));
}

void TestSchemaValidator::validate_fileIndexed_shouldReportError()
{
    // Un champ File ne peut pas être 'indexed'.
    SchemaValidator validator;

    Field file = makeField("avatar", FieldType::File);
    file.indexed = true;
    sea::domain::FileFieldConfig cfg{};
    cfg.storage_path = "users/avatars";
    file.file_config = cfg;

    Schema schema = makeSchema({ makeEntity("User", { file }) });

    const auto errors = validator.validate(schema);
    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "cannot be 'indexed'"));
}

void TestSchemaValidator::validate_fileWithMaxLength_shouldReportError()
{
    // max_length n'est pas applicable à un champ File.
    SchemaValidator validator;

    Field file = makeField("avatar", FieldType::File);
    file.max_length = std::size_t(255);
    sea::domain::FileFieldConfig cfg{};
    cfg.storage_path = "users/avatars";
    file.file_config = cfg;

    Schema schema = makeSchema({ makeEntity("User", { file }) });

    const auto errors = validator.validate(schema);
    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "cannot have 'max_length'"));
}

void TestSchemaValidator::validate_fileWithMinMaxValue_shouldReportError()
{
    // min_value / max_value ne sont pas applicables à un champ File.
    SchemaValidator validator;

    Field file = makeField("avatar", FieldType::File);
    file.min_value = std::int64_t(0);
    sea::domain::FileFieldConfig cfg{};
    cfg.storage_path = "users/avatars";
    file.file_config = cfg;

    Schema schema = makeSchema({ makeEntity("User", { file }) });

    const auto errors = validator.validate(schema);
    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "cannot have 'min_value' or 'max_value'"));
}

void TestSchemaValidator::validate_fileWithZeroMaxSize_shouldReportError()
{
    SchemaValidator validator;

    Field file = makeField("avatar", FieldType::File);
    sea::domain::FileFieldConfig cfg{};
    cfg.storage_path = "users/avatars";
    cfg.max_size_bytes = std::size_t(0);
    file.file_config = cfg;

    Schema schema = makeSchema({ makeEntity("User", { file }) });

    const auto errors = validator.validate(schema);
    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "max_size_bytes = 0"));
}

void TestSchemaValidator::validate_fileWithHugeMaxSize_shouldReportWarning()
{
    // Une taille > 10 GiB produit un message préfixé [warning].
    SchemaValidator validator;

    Field file = makeField("avatar", FieldType::File);
    sea::domain::FileFieldConfig cfg{};
    cfg.storage_path = "users/avatars";
    cfg.max_size_bytes = std::size_t(20ULL * 1024 * 1024 * 1024);   // 20 GiB
    file.file_config = cfg;

    Schema schema = makeSchema({ makeEntity("User", { file }) });

    const auto errors = validator.validate(schema);
    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "[warning]"));
    QVERIFY(containsError(errors, "max_size > 10 GiB"));
}

// ── 4. Relations ──────────────────────────────────────────────

void TestSchemaValidator::validate_relationUnnamed_shouldReportError()
{
    SchemaValidator validator;

    Entity user = makeEntity("User", { makeField("id", FieldType::UUID) });

    sea::domain::Relation relation{};
    relation.name = "";
    relation.target_entity = "Post";
    relation.kind = RelationKind::HasMany;
    user.relations.push_back(relation);

    Schema schema = makeSchema({ user });

    const auto errors = validator.validate(schema);
    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "unnamed relation"));
}

void TestSchemaValidator::validate_relationDuplicate_shouldReportError()
{
    SchemaValidator validator;

    Entity post = makeEntity("Post", { makeField("id", FieldType::UUID) });
    Entity user = makeEntity("User", { makeField("id", FieldType::UUID) });

    sea::domain::Relation r1{};
    r1.name = "posts";
    r1.target_entity = "Post";
    r1.kind = RelationKind::HasMany;

    sea::domain::Relation r2{};
    r2.name = "posts";   // doublon
    r2.target_entity = "Post";
    r2.kind = RelationKind::HasMany;

    user.relations.push_back(r1);
    user.relations.push_back(r2);

    Schema schema = makeSchema({ user, post });

    const auto errors = validator.validate(schema);
    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "Duplicate relation"));
}

void TestSchemaValidator::validate_relationWithoutTarget_shouldReportError()
{
    SchemaValidator validator;

    Entity user = makeEntity("User", { makeField("id", FieldType::UUID) });

    sea::domain::Relation relation{};
    relation.name = "posts";
    relation.target_entity = "";
    relation.kind = RelationKind::HasMany;
    user.relations.push_back(relation);

    Schema schema = makeSchema({ user });

    const auto errors = validator.validate(schema);
    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "no target_entity"));
}

// ── 5. Pagination ─────────────────────────────────────────────

void TestSchemaValidator::validate_pagePaginationMaxSizeZero_shouldReportError()
{
    SchemaValidator validator;

    Entity entity = makeEntity("User", {
                                           makeField("id", FieldType::UUID),
                                           makeField("name", FieldType::String)
                                       });

    sea::domain::PagePagination page{};
    page.default_page_size = 20;
    page.max_page_size = 0;

    sea::domain::PaginationConfig pagination{};
    pagination.page = page;
    entity.pagination = pagination;

    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);
    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "max_page_size = 0"));
}

void TestSchemaValidator::validate_pagePaginationSortFieldNotWhitelisted_shouldReportError()
{
    // default_sort référence un champ qui existe dans l'entité mais
    // n'est pas dans sortable_fields.
    SchemaValidator validator;

    Entity entity = makeEntity("User", {
                                           makeField("id", FieldType::UUID),
                                           makeField("name", FieldType::String),
                                           makeField("created_at", FieldType::Timestamp)
                                       });

    sea::domain::PagePagination page{};
    page.default_page_size = 20;
    page.max_page_size = 100;
    page.sortable_fields = { "name" };               // created_at absent
    page.default_sort = std::string("created_at:asc");

    sea::domain::PaginationConfig pagination{};
    pagination.page = page;
    entity.pagination = pagination;

    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);
    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "not in sortable_fields"));
}

void TestSchemaValidator::validate_offsetPaginationDefaultLimitZero_shouldReportError()
{
    SchemaValidator validator;

    Entity entity = makeEntity("User", {
                                           makeField("id", FieldType::UUID),
                                           makeField("name", FieldType::String)
                                       });

    sea::domain::OffsetPagination offset{};
    offset.default_limit = 0;
    offset.max_limit = 100;

    sea::domain::PaginationConfig pagination{};
    pagination.offset = offset;
    entity.pagination = pagination;

    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);
    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "default_limit = 0"));
}

void TestSchemaValidator::validate_offsetPaginationUnknownSortableField_shouldReportError()
{
    SchemaValidator validator;

    Entity entity = makeEntity("User", {
                                           makeField("id", FieldType::UUID),
                                           makeField("name", FieldType::String)
                                       });

    sea::domain::OffsetPagination offset{};
    offset.default_limit = 20;
    offset.max_limit = 100;
    offset.sortable_fields = { "ghost_field" };

    sea::domain::PaginationConfig pagination{};
    pagination.offset = offset;
    entity.pagination = pagination;

    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);
    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "unknown sortable_field"));
}

void TestSchemaValidator::validate_cursorPaginationEmptyCursorField_shouldReportError()
{
    SchemaValidator validator;

    Entity entity = makeEntity("User", {
                                           makeField("id", FieldType::UUID),
                                           makeField("name", FieldType::String)
                                       });

    sea::domain::CursorPagination cursor{};
    cursor.default_limit = 20;
    cursor.max_limit = 100;
    cursor.cursor_field = "";              // vide
    cursor.sort = "id:asc";

    sea::domain::PaginationConfig pagination{};
    pagination.cursor = cursor;
    entity.pagination = pagination;

    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);
    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "no cursor_field"));
}

void TestSchemaValidator::validate_cursorPaginationMalformedSort_shouldReportError()
{
    SchemaValidator validator;

    Entity entity = makeEntity("User", {
                                           makeField("id", FieldType::UUID),
                                           makeField("name", FieldType::String)
                                       });

    sea::domain::CursorPagination cursor{};
    cursor.default_limit = 20;
    cursor.max_limit = 100;
    cursor.cursor_field = "id";
    cursor.sort = "id:sideways";            // direction invalide

    sea::domain::PaginationConfig pagination{};
    pagination.cursor = cursor;
    entity.pagination = pagination;

    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);
    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "malformed 'sort'"));
}

void TestSchemaValidator::validate_cursorPaginationDefaultGreaterThanMax_shouldReportError()
{
    SchemaValidator validator;

    Entity entity = makeEntity("User", {
                                           makeField("id", FieldType::UUID),
                                           makeField("name", FieldType::String)
                                       });

    sea::domain::CursorPagination cursor{};
    cursor.default_limit = 200;
    cursor.max_limit = 100;
    cursor.cursor_field = "id";
    cursor.sort = "id:asc";

    sea::domain::PaginationConfig pagination{};
    pagination.cursor = cursor;
    entity.pagination = pagination;

    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);
    QVERIFY(!errors.empty());
    QVERIFY(containsError(errors, "default_limit > max_limit"));
}

void TestSchemaValidator::validate_cursorPaginationMultiSort_shouldPass()
{
    // Le sort cursor accepte un multi-tri "f1:asc,f2:desc" tant que
    // chaque champ existe dans l'entité.
    SchemaValidator validator;

    Entity entity = makeEntity("User", {
                                           makeField("id", FieldType::UUID),
                                           makeField("name", FieldType::String),
                                           makeField("created_at", FieldType::Timestamp)
                                       });

    sea::domain::CursorPagination cursor{};
    cursor.default_limit = 20;
    cursor.max_limit = 100;
    cursor.cursor_field = "id";
    cursor.sort = "created_at:desc,id:asc";

    sea::domain::PaginationConfig pagination{};
    pagination.cursor = cursor;
    entity.pagination = pagination;

    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);
    QVERIFY(errors.empty());
}

// ── 6. Accumulation d'erreurs ─────────────────────────────────

void TestSchemaValidator::validate_multipleErrors_shouldAllBeReported()
{
    // validate() n'arrête pas au premier problème : il accumule
    // toutes les erreurs rencontrées.
    SchemaValidator validator;

    Field badName = makeField("first-name", FieldType::String);   // tiret invalide
    Field pwd = makeField("password", FieldType::Password);
    pwd.serializable = true;                                       // erreur 2

    Entity entity = makeEntity("User", { badName, pwd });

    Schema schema = makeSchema({ entity });

    const auto errors = validator.validate(schema);

    // Au moins deux erreurs distinctes attendues.
    QVERIFY(errors.size() >= std::size_t(2));
    QVERIFY(containsError(errors, "Invalid field name"));
    QVERIFY(containsError(errors, "should not be serializable"));
}