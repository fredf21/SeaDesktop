#include "test_schema_runtime_registry.h"

#include "runtime/schema_runtime_registry.h"
#include "schema.h"
#include "entity.h"
#include "field.h"
#include "field_type.h"

#include <string>

using sea::infrastructure::runtime::SchemaRuntimeRegistry;
using sea::domain::Schema;
using sea::domain::Entity;
using sea::domain::Field;
using sea::domain::FieldType;

namespace {

// Construit un Field minimal (nom + type).
Field makeField(const std::string& name, FieldType type) {
    Field f{};
    f.name = name;
    f.type = type;
    return f;
}

// Construit une Entity avec un id (uuid) et un champ supplémentaire.
Entity makeEntity(const std::string& name,
                  const std::string& extraField,
                  FieldType extraType) {
    Entity e{};
    e.name = name;
    e.fields.push_back(makeField("id", FieldType::UUID));
    e.fields.push_back(makeField(extraField, extraType));
    return e;
}

// Construit un Schema à partir d'une liste d'entités.
Schema makeSchema(std::vector<Entity> entities) {
    Schema s{};
    s.entities = std::move(entities);
    return s;
}

} // namespace

// ═════════════════════════════════════════════════════════════
// 1. register_schema / find_entity
// ═════════════════════════════════════════════════════════════

void TestSchemaRuntimeRegistry::emptyRegistry_findEntity_shouldReturnNull() {
    SchemaRuntimeRegistry registry;
    QCOMPARE(registry.find_entity("User"), nullptr);
}

void TestSchemaRuntimeRegistry::registerSchema_findKnownEntity_shouldReturnPointer() {
    SchemaRuntimeRegistry registry;
    registry.register_schema(makeSchema({
        makeEntity("User", "email", FieldType::Email),
    }));

    const Entity* found = registry.find_entity("User");
    QVERIFY(found != nullptr);
    QCOMPARE(QString::fromStdString(found->name), QString("User"));
}

void TestSchemaRuntimeRegistry::findEntity_unknownName_shouldReturnNull() {
    SchemaRuntimeRegistry registry;
    registry.register_schema(makeSchema({
        makeEntity("User", "email", FieldType::Email),
    }));

    QCOMPARE(registry.find_entity("Product"), nullptr);
}

void TestSchemaRuntimeRegistry::findEntity_isCaseSensitive() {
    // La recherche se fait sur le nom exact : "user" != "User".
    SchemaRuntimeRegistry registry;
    registry.register_schema(makeSchema({
        makeEntity("User", "email", FieldType::Email),
    }));

    QVERIFY(registry.find_entity("User") != nullptr);
    QCOMPARE(registry.find_entity("user"), nullptr);
    QCOMPARE(registry.find_entity("USER"), nullptr);
}

void TestSchemaRuntimeRegistry::registerEmptySchema_shouldSucceed() {
    SchemaRuntimeRegistry registry;
    verifyNoThrow([&]() {
        registry.register_schema(makeSchema({}));
    });
    QCOMPARE(registry.find_entity("Anything"), nullptr);
}

void TestSchemaRuntimeRegistry::registerMultipleEntities_allFindable() {
    SchemaRuntimeRegistry registry;
    registry.register_schema(makeSchema({
        makeEntity("User",    "email", FieldType::Email),
        makeEntity("Product", "price", FieldType::Float),
        makeEntity("Order",   "total", FieldType::Decimal),
    }));

    QVERIFY(registry.find_entity("User") != nullptr);
    QVERIFY(registry.find_entity("Product") != nullptr);
    QVERIFY(registry.find_entity("Order") != nullptr);
    QCOMPARE(QString::fromStdString(registry.find_entity("Product")->name),
             QString("Product"));
}

// ═════════════════════════════════════════════════════════════
// 2. has_entity
// ═════════════════════════════════════════════════════════════

void TestSchemaRuntimeRegistry::hasEntity_knownEntity_shouldReturnTrue() {
    SchemaRuntimeRegistry registry;
    registry.register_schema(makeSchema({
        makeEntity("User", "email", FieldType::Email),
    }));

    QCOMPARE(registry.has_entity("User"), true);
}

void TestSchemaRuntimeRegistry::hasEntity_unknownEntity_shouldReturnFalse() {
    SchemaRuntimeRegistry registry;
    registry.register_schema(makeSchema({
        makeEntity("User", "email", FieldType::Email),
    }));

    QCOMPARE(registry.has_entity("Ghost"), false);
}

void TestSchemaRuntimeRegistry::hasEntity_onEmptyRegistry_shouldReturnFalse() {
    SchemaRuntimeRegistry registry;
    QCOMPARE(registry.has_entity("User"), false);
}

// ═════════════════════════════════════════════════════════════
// 3. find_field
// ═════════════════════════════════════════════════════════════

void TestSchemaRuntimeRegistry::findField_knownEntityKnownField_shouldReturnPointer() {
    SchemaRuntimeRegistry registry;
    registry.register_schema(makeSchema({
        makeEntity("User", "email", FieldType::Email),
    }));

    const Field* field = registry.find_field("User", "email");
    QVERIFY(field != nullptr);
    QCOMPARE(QString::fromStdString(field->name), QString("email"));
}

void TestSchemaRuntimeRegistry::findField_knownEntityUnknownField_shouldReturnNull() {
    SchemaRuntimeRegistry registry;
    registry.register_schema(makeSchema({
        makeEntity("User", "email", FieldType::Email),
    }));

    QCOMPARE(registry.find_field("User", "nonexistent"), nullptr);
}

void TestSchemaRuntimeRegistry::findField_unknownEntity_shouldReturnNull() {
    SchemaRuntimeRegistry registry;
    registry.register_schema(makeSchema({
        makeEntity("User", "email", FieldType::Email),
    }));

    QCOMPARE(registry.find_field("Ghost", "email"), nullptr);
}

void TestSchemaRuntimeRegistry::findField_returnsCorrectFieldData() {
    SchemaRuntimeRegistry registry;
    registry.register_schema(makeSchema({
        makeEntity("Product", "price", FieldType::Float),
    }));

    // L'entité a deux champs : "id" (UUID) et "price" (Float).
    const Field* idField = registry.find_field("Product", "id");
    const Field* priceField = registry.find_field("Product", "price");

    QVERIFY(idField != nullptr);
    QVERIFY(priceField != nullptr);
    QCOMPARE(idField->type, FieldType::UUID);
    QCOMPARE(priceField->type, FieldType::Float);
}

// ═════════════════════════════════════════════════════════════
// 4. clear et ré-enregistrement
// ═════════════════════════════════════════════════════════════

void TestSchemaRuntimeRegistry::clear_emptiesRegistry() {
    SchemaRuntimeRegistry registry;
    registry.register_schema(makeSchema({
        makeEntity("User", "email", FieldType::Email),
    }));
    QVERIFY(registry.has_entity("User"));

    registry.clear();
    QCOMPARE(registry.has_entity("User"), false);
    QCOMPARE(registry.find_entity("User"), nullptr);
}

void TestSchemaRuntimeRegistry::registerSchema_replacesPreviousContent() {
    // register_schema vide le contenu précédent avant d'insérer.
    SchemaRuntimeRegistry registry;

    registry.register_schema(makeSchema({
        makeEntity("User", "email", FieldType::Email),
    }));
    QVERIFY(registry.has_entity("User"));

    // Nouvel enregistrement : User disparaît, Product apparaît.
    registry.register_schema(makeSchema({
        makeEntity("Product", "price", FieldType::Float),
    }));

    QCOMPARE(registry.has_entity("User"), false);
    QCOMPARE(registry.has_entity("Product"), true);
}

void TestSchemaRuntimeRegistry::clearThenRegister_shouldWork() {
    SchemaRuntimeRegistry registry;
    registry.register_schema(makeSchema({
        makeEntity("User", "email", FieldType::Email),
    }));

    registry.clear();
    QVERIFY(!registry.has_entity("User"));

    registry.register_schema(makeSchema({
        makeEntity("Order", "total", FieldType::Decimal),
    }));
    QCOMPARE(registry.has_entity("Order"), true);
}