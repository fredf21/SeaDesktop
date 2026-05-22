#include "test_generic_validator.h"

#include "runtime/generic_validator.h"
#include "runtime/dynamic_record.h"
#include "entity.h"
#include "field.h"
#include "field_type.h"

#include <cstdint>
#include <string>

using sea::infrastructure::runtime::GenericValidator;
using sea::infrastructure::runtime::DynamicRecord;
using sea::infrastructure::runtime::DynamicValue;
using sea::domain::Entity;
using sea::domain::Field;
using sea::domain::FieldType;

namespace {

// Construit un Field (nom + type + required).
Field makeField(const std::string& name, FieldType type, bool required = true) {
    Field f{};
    f.name     = name;
    f.type     = type;
    f.required = required;
    return f;
}

// Construit une Entity à partir d'une liste de champs.
Entity makeEntity(std::vector<Field> fields) {
    Entity e{};
    e.name   = "TestEntity";
    e.fields = std::move(fields);
    return e;
}

} // namespace

// ═════════════════════════════════════════════════════════════
// 1. validate : record valide
// ═════════════════════════════════════════════════════════════

void TestGenericValidator::validate_validRecord_shouldReturnNoErrors() {
    GenericValidator validator;

    Entity entity = makeEntity({
        makeField("name", FieldType::String),
        makeField("age",  FieldType::Int),
    });

    DynamicRecord record;
    record["name"] = std::string("Alice");
    record["age"]  = std::int32_t(30);

    const auto errors = validator.validate(entity, record);
    QVERIFY(errors.empty());
}

void TestGenericValidator::validate_emptyEntity_shouldReturnNoErrors() {
    GenericValidator validator;
    Entity entity = makeEntity({});
    DynamicRecord record;

    const auto errors = validator.validate(entity, record);
    QVERIFY(errors.empty());
}

void TestGenericValidator::validate_allFieldTypes_validRecord_shouldPass() {
    GenericValidator validator;

    Entity entity = makeEntity({
        makeField("s_string",   FieldType::String),
        makeField("s_text",     FieldType::Text),
        makeField("s_uuid",     FieldType::UUID),
        makeField("n_int",      FieldType::Int),
        makeField("n_smallint", FieldType::SmallInt),
        makeField("n_bigint",   FieldType::BigInt),
        makeField("n_float",    FieldType::Float),
        makeField("b_bool",     FieldType::Bool),
    });

    DynamicRecord record;
    record["s_string"]   = std::string("hello");
    record["s_text"]     = std::string("long text");
    record["s_uuid"]     = std::string("550e8400-e29b-41d4-a716-446655440000");
    record["n_int"]      = std::int32_t(42);
    record["n_smallint"] = std::int16_t(10);
    record["n_bigint"]   = std::int64_t(9000000000LL);
    record["n_float"]    = double(3.14);
    record["b_bool"]     = true;

    const auto errors = validator.validate(entity, record);
    QVERIFY(errors.empty());
}

// ═════════════════════════════════════════════════════════════
// 2. validate : champs requis manquants / null
// ═════════════════════════════════════════════════════════════

void TestGenericValidator::validate_missingRequiredField_shouldReportError() {
    GenericValidator validator;

    Entity entity = makeEntity({
        makeField("name",  FieldType::String, /*required=*/true),
        makeField("email", FieldType::Email,  /*required=*/true),
    });

    DynamicRecord record;
    record["name"] = std::string("Bob");
    // "email" absent

    const auto errors = validator.validate(entity, record);
    QCOMPARE(errors.size(), std::size_t(1));
}

void TestGenericValidator::validate_missingOptionalField_shouldPass() {
    GenericValidator validator;

    Entity entity = makeEntity({
        makeField("name",     FieldType::String, /*required=*/true),
        makeField("nickname", FieldType::String, /*required=*/false),
    });

    DynamicRecord record;
    record["name"] = std::string("Bob");
    // "nickname" absent mais optionnel

    const auto errors = validator.validate(entity, record);
    QVERIFY(errors.empty());
}

void TestGenericValidator::validate_missingRequiredFieldWithDefault_shouldPass() {
    // Un champ requis absent mais doté d'une valeur par défaut ne
    // déclenche pas d'erreur (has_default() vrai).
    GenericValidator validator;

    Field withDefault = makeField("status", FieldType::String, /*required=*/true);
    withDefault.default_val = std::string("active");

    Entity entity = makeEntity({
        makeField("name", FieldType::String, /*required=*/true),
        withDefault,
    });

    DynamicRecord record;
    record["name"] = std::string("Bob");
    // "status" absent mais a un default

    const auto errors = validator.validate(entity, record);
    QVERIFY(errors.empty());
}

void TestGenericValidator::validate_nullRequiredField_shouldReportError() {
    GenericValidator validator;

    Entity entity = makeEntity({
        makeField("name", FieldType::String, /*required=*/true),
    });

    DynamicRecord record;
    record["name"] = std::monostate{};   // null logique

    const auto errors = validator.validate(entity, record);
    QCOMPARE(errors.size(), std::size_t(1));
}

void TestGenericValidator::validate_nullOptionalField_shouldPass() {
    GenericValidator validator;

    Entity entity = makeEntity({
        makeField("nickname", FieldType::String, /*required=*/false),
    });

    DynamicRecord record;
    record["nickname"] = std::monostate{};

    const auto errors = validator.validate(entity, record);
    QVERIFY(errors.empty());
}

// ═════════════════════════════════════════════════════════════
// 3. validate : erreurs de type
// ═════════════════════════════════════════════════════════════

void TestGenericValidator::validate_wrongTypeStringField_shouldReportError() {
    // Un champ String recevant un entier : erreur de type.
    GenericValidator validator;

    Entity entity = makeEntity({
        makeField("name", FieldType::String),
    });

    DynamicRecord record;
    record["name"] = std::int64_t(123);   // devrait être string

    const auto errors = validator.validate(entity, record);
    QCOMPARE(errors.size(), std::size_t(1));
}

void TestGenericValidator::validate_wrongTypeIntField_shouldReportError() {
    // Un champ Int recevant une string : erreur de type.
    GenericValidator validator;

    Entity entity = makeEntity({
        makeField("age", FieldType::Int),
    });

    DynamicRecord record;
    record["age"] = std::string("not a number");

    const auto errors = validator.validate(entity, record);
    QCOMPARE(errors.size(), std::size_t(1));
}

void TestGenericValidator::validate_wrongTypeBoolField_shouldReportError() {
    GenericValidator validator;

    Entity entity = makeEntity({
        makeField("active", FieldType::Bool),
    });

    DynamicRecord record;
    record["active"] = std::string("true");   // string, pas bool

    const auto errors = validator.validate(entity, record);
    QCOMPARE(errors.size(), std::size_t(1));
}

// ═════════════════════════════════════════════════════════════
// 4. validate : email
// ═════════════════════════════════════════════════════════════

void TestGenericValidator::validate_validEmail_shouldPass() {
    GenericValidator validator;

    Entity entity = makeEntity({
        makeField("email", FieldType::Email),
    });

    DynamicRecord record;
    record["email"] = std::string("alice@example.com");

    const auto errors = validator.validate(entity, record);
    QVERIFY(errors.empty());
}

void TestGenericValidator::validate_invalidEmail_shouldReportError() {
    GenericValidator validator;

    Entity entity = makeEntity({
        makeField("email", FieldType::Email),
    });

    DynamicRecord record;
    record["email"] = std::string("not-an-email");

    const auto errors = validator.validate(entity, record);
    QCOMPARE(errors.size(), std::size_t(1));
}

void TestGenericValidator::validate_emailWithoutAt_shouldReportError() {
    GenericValidator validator;

    Entity entity = makeEntity({
        makeField("email", FieldType::Email),
    });

    DynamicRecord record;
    record["email"] = std::string("alice.example.com");

    const auto errors = validator.validate(entity, record);
    QCOMPARE(errors.size(), std::size_t(1));
}

// ═════════════════════════════════════════════════════════════
// 5. validate : max_length
// ═════════════════════════════════════════════════════════════

void TestGenericValidator::validate_stringWithinMaxLength_shouldPass() {
    GenericValidator validator;

    Field nameField = makeField("name", FieldType::String);
    nameField.max_length = std::size_t(10);

    Entity entity = makeEntity({ nameField });

    DynamicRecord record;
    record["name"] = std::string("short");   // 5 <= 10

    const auto errors = validator.validate(entity, record);
    QVERIFY(errors.empty());
}

void TestGenericValidator::validate_stringExceedingMaxLength_shouldReportError() {
    GenericValidator validator;

    Field nameField = makeField("name", FieldType::String);
    nameField.max_length = std::size_t(5);

    Entity entity = makeEntity({ nameField });

    DynamicRecord record;
    record["name"] = std::string("way too long");   // > 5

    const auto errors = validator.validate(entity, record);
    QCOMPARE(errors.size(), std::size_t(1));
}

void TestGenericValidator::validate_stringExactlyMaxLength_shouldPass() {
    // La contrainte est size() > max_length : la valeur exacte passe.
    GenericValidator validator;

    Field nameField = makeField("name", FieldType::String);
    nameField.max_length = std::size_t(5);

    Entity entity = makeEntity({ nameField });

    DynamicRecord record;
    record["name"] = std::string("12345");   // exactement 5

    const auto errors = validator.validate(entity, record);
    QVERIFY(errors.empty());
}

// ═════════════════════════════════════════════════════════════
// 6. validate : min_value / max_value
// ═════════════════════════════════════════════════════════════

void TestGenericValidator::validate_intWithinRange_shouldPass() {
    GenericValidator validator;

    Field qty = makeField("quantity", FieldType::Int);
    qty.min_value = std::int32_t(0);
    qty.max_value = std::int32_t(100);

    Entity entity = makeEntity({ qty });

    DynamicRecord record;
    record["quantity"] = std::int32_t(50);

    const auto errors = validator.validate(entity, record);
    QVERIFY(errors.empty());
}

void TestGenericValidator::validate_intBelowMin_shouldReportError() {
    GenericValidator validator;

    Field qty = makeField("quantity", FieldType::Int);
    qty.min_value = std::int32_t(10);

    Entity entity = makeEntity({ qty });

    DynamicRecord record;
    record["quantity"] = std::int32_t(5);   // < 10

    const auto errors = validator.validate(entity, record);
    QCOMPARE(errors.size(), std::size_t(1));
}

void TestGenericValidator::validate_intAboveMax_shouldReportError() {
    GenericValidator validator;

    Field qty = makeField("quantity", FieldType::Int);
    qty.max_value = std::int32_t(100);

    Entity entity = makeEntity({ qty });

    DynamicRecord record;
    record["quantity"] = std::int32_t(500);   // > 100

    const auto errors = validator.validate(entity, record);
    QCOMPARE(errors.size(), std::size_t(1));
}

void TestGenericValidator::validate_floatWithinRange_shouldPass() {
    GenericValidator validator;

    Field rating = makeField("rating", FieldType::Float);
    rating.min_value = double(0.0);
    rating.max_value = double(5.0);

    Entity entity = makeEntity({ rating });

    DynamicRecord record;
    record["rating"] = double(4.5);

    const auto errors = validator.validate(entity, record);
    QVERIFY(errors.empty());
}

void TestGenericValidator::validate_floatBelowMin_shouldReportError() {
    GenericValidator validator;

    Field rating = makeField("rating", FieldType::Float);
    rating.min_value = double(1.0);

    Entity entity = makeEntity({ rating });

    DynamicRecord record;
    record["rating"] = double(0.5);   // < 1.0

    const auto errors = validator.validate(entity, record);
    QCOMPARE(errors.size(), std::size_t(1));
}

// ═════════════════════════════════════════════════════════════
// 7. validate_partial : tolérance des champs absents
// ═════════════════════════════════════════════════════════════

void TestGenericValidator::validatePartial_missingRequiredField_shouldPass() {
    // En update partiel, un champ requis absent n'est PAS une erreur.
    GenericValidator validator;

    Entity entity = makeEntity({
        makeField("name",  FieldType::String, /*required=*/true),
        makeField("email", FieldType::Email,  /*required=*/true),
    });

    DynamicRecord record;
    record["name"] = std::string("Bob");
    // "email" absent — toléré en partiel

    const auto errors = validator.validate_partial(entity, record);
    QVERIFY(errors.empty());
}

void TestGenericValidator::validatePartial_presentInvalidField_shouldReportError() {
    // Un champ PRÉSENT mais invalide est quand même rejeté en partiel.
    GenericValidator validator;

    Entity entity = makeEntity({
        makeField("email", FieldType::Email),
    });

    DynamicRecord record;
    record["email"] = std::string("invalid-email");

    const auto errors = validator.validate_partial(entity, record);
    QCOMPARE(errors.size(), std::size_t(1));
}

void TestGenericValidator::validatePartial_nullRequiredField_shouldReportError() {
    // Mettre explicitement un champ requis à null reste une erreur,
    // même en update partiel.
    GenericValidator validator;

    Entity entity = makeEntity({
        makeField("name", FieldType::String, /*required=*/true),
    });

    DynamicRecord record;
    record["name"] = std::monostate{};

    const auto errors = validator.validate_partial(entity, record);
    QCOMPARE(errors.size(), std::size_t(1));
}

void TestGenericValidator::validatePartial_emptyRecord_shouldReturnNoErrors() {
    GenericValidator validator;

    Entity entity = makeEntity({
        makeField("name",  FieldType::String, /*required=*/true),
        makeField("email", FieldType::Email,  /*required=*/true),
    });

    DynamicRecord record;   // vide

    const auto errors = validator.validate_partial(entity, record);
    QVERIFY(errors.empty());
}

// ═════════════════════════════════════════════════════════════
// 8. Le champ "id" est toujours ignoré
// ═════════════════════════════════════════════════════════════

void TestGenericValidator::validate_idFieldIsAlwaysIgnored() {
    // Le champ nommé "id" est sauté : même requis et absent, pas
    // d'erreur ; même présent avec un mauvais type, pas d'erreur.
    GenericValidator validator;

    Entity entity = makeEntity({
        makeField("id",   FieldType::UUID, /*required=*/true),
        makeField("name", FieldType::String),
    });

    DynamicRecord record;
    record["name"] = std::string("Alice");
    // "id" absent — ne doit PAS générer d'erreur

    const auto errors = validator.validate(entity, record);
    QVERIFY(errors.empty());
}

void TestGenericValidator::validatePartial_idFieldIsAlwaysIgnored() {
    GenericValidator validator;

    Entity entity = makeEntity({
        makeField("id",   FieldType::UUID, /*required=*/true),
        makeField("name", FieldType::String),
    });

    DynamicRecord record;
    // "id" présent avec un type incohérent — ignoré quand même
    record["id"] = std::int64_t(999);

    const auto errors = validator.validate_partial(entity, record);
    QVERIFY(errors.empty());
}