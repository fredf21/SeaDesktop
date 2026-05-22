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