#include "test_json_record_parser.h"

#include "runtime/json_record_parser.h"
#include "runtime/dynamic_record.h"
#include "exception_handling.h"
#include "entity.h"
#include "field.h"
#include "field_type.h"

#include <cstdint>
#include <string>
#include <variant>

using sea::infrastructure::runtime::JsonRecordParser;
using sea::infrastructure::runtime::DynamicRecord;
using sea::infrastructure::runtime::DynamicValue;
using sea::domain::Entity;
using sea::domain::Field;
using sea::domain::FieldType;
using RuntimeError = sea::sea_errors_handling::RUNTIME_EXECUTION;

namespace {

// Construit un Field (nom + type, + drapeau unsigned optionnel).
Field makeField(const std::string& name, FieldType type,
                bool unsignedValue = false) {
    Field f{};
    f.name           = name;
    f.type           = type;
    f.unsigned_value = unsignedValue;
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
// 1. Erreurs globales
// ═════════════════════════════════════════════════════════════

void TestJsonRecordParser::parse_invalidJson_shouldThrow() {
    JsonRecordParser parser;
    Entity entity = makeEntity({ makeField("name", FieldType::String) });

    verifyThrows<RuntimeError>([&]() {
        [[maybe_unused]] auto r = parser.parse(entity, "{ this is not json");
    });
}

void TestJsonRecordParser::parse_emptyString_shouldThrow() {
    JsonRecordParser parser;
    Entity entity = makeEntity({ makeField("name", FieldType::String) });

    verifyThrows<RuntimeError>([&]() {
        [[maybe_unused]] auto r = parser.parse(entity, "");
    });
}

void TestJsonRecordParser::parse_jsonArray_shouldThrow() {
    // Un body JSON valide mais qui n'est pas un objet : rejeté
    // (std::runtime_error d'après l'implémentation).
    JsonRecordParser parser;
    Entity entity = makeEntity({ makeField("name", FieldType::String) });

    verifyThrows<std::runtime_error>([&]() {
        [[maybe_unused]] auto r = parser.parse(entity, R"(["a", "b", "c"])");
    });
}

void TestJsonRecordParser::parse_jsonScalar_shouldThrow() {
    JsonRecordParser parser;
    Entity entity = makeEntity({ makeField("name", FieldType::String) });

    verifyThrows<std::runtime_error>([&]() {
        [[maybe_unused]] auto r = parser.parse(entity, "42");
    });
}

// ═════════════════════════════════════════════════════════════
// 2. Extraction des champs déclarés / clés inconnues
// ═════════════════════════════════════════════════════════════

void TestJsonRecordParser::parse_emptyObject_shouldReturnEmptyRecord() {
    JsonRecordParser parser;
    Entity entity = makeEntity({ makeField("name", FieldType::String) });

    const auto record = parser.parse(entity, "{}");
    QVERIFY(record.empty());
}

void TestJsonRecordParser::parse_declaredFields_shouldBeExtracted() {
    JsonRecordParser parser;
    Entity entity = makeEntity({
        makeField("name", FieldType::String),
        makeField("age",  FieldType::Int),
    });

    const auto record = parser.parse(entity, R"({"name": "Alice", "age": 30})");

    QCOMPARE(record.size(), std::size_t(2));
    QVERIFY(record.count("name") == 1);
    QVERIFY(record.count("age") == 1);
}

void TestJsonRecordParser::parse_unknownJsonKeys_shouldBeIgnored() {
    // Les clés JSON non déclarées dans l'entité sont ignorées :
    // le parser n'itère que sur entity.fields.
    JsonRecordParser parser;
    Entity entity = makeEntity({ makeField("name", FieldType::String) });

    const auto record = parser.parse(
        entity, R"({"name": "Alice", "unknown_key": "ignored", "extra": 99})");

    QCOMPARE(record.size(), std::size_t(1));
    QVERIFY(record.count("name") == 1);
    QVERIFY(record.count("unknown_key") == 0);
    QVERIFY(record.count("extra") == 0);
}

void TestJsonRecordParser::parse_declaredFieldAbsentFromJson_shouldBeOmitted() {
    // Un champ déclaré mais absent du JSON n'apparaît pas dans le record.
    JsonRecordParser parser;
    Entity entity = makeEntity({
        makeField("name",  FieldType::String),
        makeField("email", FieldType::Email),
    });

    const auto record = parser.parse(entity, R"({"name": "Alice"})");

    QCOMPARE(record.size(), std::size_t(1));
    QVERIFY(record.count("name") == 1);
    QVERIFY(record.count("email") == 0);
}

// ═════════════════════════════════════════════════════════════
// 3. Valeurs null -> monostate
// ═════════════════════════════════════════════════════════════

void TestJsonRecordParser::parse_nullValue_shouldBecomeMonostate() {
    JsonRecordParser parser;
    Entity entity = makeEntity({ makeField("nickname", FieldType::String) });

    const auto record = parser.parse(entity, R"({"nickname": null})");

    QVERIFY(record.count("nickname") == 1);
    QVERIFY(std::holds_alternative<std::monostate>(record.at("nickname")));
}

void TestJsonRecordParser::parse_nullValueForRequiredField_shouldStillBecomeMonostate() {
    // Le parser ne juge pas du caractère requis : null -> monostate
    // quel que soit le champ. (C'est le validateur qui jugera.)
    JsonRecordParser parser;
    Field required = makeField("name", FieldType::String);
    required.required = true;
    Entity entity = makeEntity({ required });

    const auto record = parser.parse(entity, R"({"name": null})");

    QVERIFY(record.count("name") == 1);
    QVERIFY(std::holds_alternative<std::monostate>(record.at("name")));
}

// ═════════════════════════════════════════════════════════════
// 4. Types scalaires
// ═════════════════════════════════════════════════════════════

void TestJsonRecordParser::parse_stringField_shouldStoreString() {
    JsonRecordParser parser;
    Entity entity = makeEntity({ makeField("name", FieldType::String) });

    const auto record = parser.parse(entity, R"({"name": "Bob"})");

    QVERIFY(std::holds_alternative<std::string>(record.at("name")));
    QCOMPARE(QString::fromStdString(std::get<std::string>(record.at("name"))),
             QString("Bob"));
}

void TestJsonRecordParser::parse_floatField_shouldStoreDouble() {
    JsonRecordParser parser;
    Entity entity = makeEntity({ makeField("score", FieldType::Float) });

    const auto record = parser.parse(entity, R"({"score": 3.14})");

    QVERIFY(std::holds_alternative<double>(record.at("score")));
    QCOMPARE(std::get<double>(record.at("score")), double(3.14));
}

void TestJsonRecordParser::parse_floatField_acceptsInteger() {
    // Un Float accepte un nombre entier JSON (is_number() est vrai).
    JsonRecordParser parser;
    Entity entity = makeEntity({ makeField("score", FieldType::Float) });

    const auto record = parser.parse(entity, R"({"score": 7})");

    QVERIFY(record.count("score") == 1);
    QVERIFY(std::holds_alternative<double>(record.at("score")));
    QCOMPARE(std::get<double>(record.at("score")), double(7.0));
}

void TestJsonRecordParser::parse_boolField_shouldStoreBool() {
    JsonRecordParser parser;
    Entity entity = makeEntity({ makeField("active", FieldType::Bool) });

    const auto record = parser.parse(entity, R"({"active": true})");

    QVERIFY(std::holds_alternative<bool>(record.at("active")));
    QCOMPARE(std::get<bool>(record.at("active")), true);
}

void TestJsonRecordParser::parse_jsonField_shouldStoreJson() {
    // Un champ Json stocke la valeur JSON telle quelle.
    JsonRecordParser parser;
    Entity entity = makeEntity({ makeField("metadata", FieldType::Json) });

    const auto record = parser.parse(
        entity, R"({"metadata": {"key": "value", "nested": [1, 2]}})");

    QVERIFY(record.count("metadata") == 1);
    QVERIFY(std::holds_alternative<nlohmann::json>(record.at("metadata")));
}

void TestJsonRecordParser::parse_jsonField_acceptsScalarValue() {
    // Un champ Json n'impose aucune forme : il accepte aussi une
    // valeur scalaire (nombre, chaîne...) — stockée telle quelle.
    JsonRecordParser parser;
    Entity entity = makeEntity({ makeField("metadata", FieldType::Json) });

    const auto record = parser.parse(entity, R"({"metadata": 42})");

    QVERIFY(record.count("metadata") == 1);
    QVERIFY(std::holds_alternative<nlohmann::json>(record.at("metadata")));
}

void TestJsonRecordParser::parse_jsonField_acceptsArrayValue() {
    // Un champ Json accepte également un tableau JSON.
    JsonRecordParser parser;
    Entity entity = makeEntity({ makeField("tags", FieldType::Json) });

    const auto record = parser.parse(entity, R"({"tags": ["a", "b", "c"]})");

    QVERIFY(record.count("tags") == 1);
    QVERIFY(std::holds_alternative<nlohmann::json>(record.at("tags")));
}

void TestJsonRecordParser::parse_uuidAndEmailFields_shouldStoreString() {
    // UUID et Email sont traités comme des strings par le parser.
    JsonRecordParser parser;
    Entity entity = makeEntity({
        makeField("id",    FieldType::UUID),
        makeField("email", FieldType::Email),
    });

    const auto record = parser.parse(
        entity,
        R"({"id": "550e8400-e29b-41d4-a716-446655440000", "email": "a@b.com"})");

    QVERIFY(std::holds_alternative<std::string>(record.at("id")));
    QVERIFY(std::holds_alternative<std::string>(record.at("email")));
}

// ═════════════════════════════════════════════════════════════
// 5. Entiers signed / unsigned
// ═════════════════════════════════════════════════════════════

void TestJsonRecordParser::parse_intField_shouldStoreInt32() {
    // Un Int signé est stocké en std::int32_t.
    JsonRecordParser parser;
    Entity entity = makeEntity({ makeField("count", FieldType::Int) });

    const auto record = parser.parse(entity, R"({"count": 42})");

    QVERIFY(std::holds_alternative<std::int32_t>(record.at("count")));
    QCOMPARE(std::get<std::int32_t>(record.at("count")), std::int32_t(42));
}

void TestJsonRecordParser::parse_bigIntField_shouldStoreInt64() {
    JsonRecordParser parser;
    Entity entity = makeEntity({ makeField("big", FieldType::BigInt) });

    const auto record = parser.parse(entity, R"({"big": 9000000000})");

    QVERIFY(std::holds_alternative<std::int64_t>(record.at("big")));
    QCOMPARE(std::get<std::int64_t>(record.at("big")),
             std::int64_t(9000000000LL));
}

void TestJsonRecordParser::parse_smallIntField_shouldStoreInt16() {
    // Un SmallInt signé est stocké en std::int16_t.
    JsonRecordParser parser;
    Entity entity = makeEntity({ makeField("small", FieldType::SmallInt) });

    const auto record = parser.parse(entity, R"({"small": 1000})");

    QVERIFY(std::holds_alternative<std::int16_t>(record.at("small")));
    QCOMPARE(std::get<std::int16_t>(record.at("small")), std::int16_t(1000));
}

void TestJsonRecordParser::parse_unsignedIntField_shouldStoreUint32() {
    // Un Int marqué unsigned_value est stocké en std::uint32_t.
    JsonRecordParser parser;
    Entity entity = makeEntity({
        makeField("stock", FieldType::Int, /*unsignedValue=*/true),
    });

    const auto record = parser.parse(entity, R"({"stock": 3000000})");

    QVERIFY(std::holds_alternative<std::uint32_t>(record.at("stock")));
    QCOMPARE(std::get<std::uint32_t>(record.at("stock")),
             std::uint32_t(3000000));
}

void TestJsonRecordParser::parse_unsignedBigIntField_shouldStoreUint64() {
    // Un BigInt marqué unsigned_value est stocké en std::uint64_t.
    JsonRecordParser parser;
    Entity entity = makeEntity({
        makeField("stock", FieldType::BigInt, /*unsignedValue=*/true),
    });

    const auto record = parser.parse(entity, R"({"stock": 3000000})");

    QVERIFY(std::holds_alternative<std::uint64_t>(record.at("stock")));
    QCOMPARE(std::get<std::uint64_t>(record.at("stock")),
             std::uint64_t(3000000));
}

void TestJsonRecordParser::parse_unsignedSmallIntField_shouldStoreUint16() {
    // Un SmallInt marqué unsigned_value est stocké en std::uint16_t.
    JsonRecordParser parser;
    Entity entity = makeEntity({
        makeField("level", FieldType::SmallInt, /*unsignedValue=*/true),
    });

    const auto record = parser.parse(entity, R"({"level": 50000})");

    QVERIFY(std::holds_alternative<std::uint16_t>(record.at("level")));
    QCOMPARE(std::get<std::uint16_t>(record.at("level")),
             std::uint16_t(50000));
}

// ═════════════════════════════════════════════════════════════
// 5b. Champ Binary (base64)
//
// Un champ Binary attend une chaîne JSON contenant du base64.
// Le parser la décode en std::vector<std::uint8_t>.
// ═════════════════════════════════════════════════════════════

void TestJsonRecordParser::parse_binaryField_shouldDecodeBase64() {
    // "SGVsbG8=" est l'encodage base64 de "Hello"
    // -> octets { 72, 101, 108, 108, 111 }.
    JsonRecordParser parser;
    Entity entity = makeEntity({ makeField("blob", FieldType::Binary) });

    const auto record = parser.parse(entity, R"({"blob": "SGVsbG8="})");

    QVERIFY(record.count("blob") == 1);
    QVERIFY(std::holds_alternative<std::vector<std::uint8_t>>(record.at("blob")));

    const auto& bytes = std::get<std::vector<std::uint8_t>>(record.at("blob"));
    const std::vector<std::uint8_t> expected{ 72, 101, 108, 108, 111 };
    QCOMPARE(bytes.size(), expected.size());
    QVERIFY(bytes == expected);
}

void TestJsonRecordParser::parse_binaryField_emptyString_shouldStoreEmptyVector() {
    // Une chaîne base64 vide décode un vecteur d'octets vide.
    JsonRecordParser parser;
    Entity entity = makeEntity({ makeField("blob", FieldType::Binary) });

    const auto record = parser.parse(entity, R"({"blob": ""})");

    QVERIFY(record.count("blob") == 1);
    QVERIFY(std::holds_alternative<std::vector<std::uint8_t>>(record.at("blob")));
    QVERIFY(std::get<std::vector<std::uint8_t>>(record.at("blob")).empty());
}

void TestJsonRecordParser::parse_binaryFieldWithNonStringValue_shouldThrow() {
    // Un champ Binary attend une chaîne base64 : un nombre est rejeté.
    JsonRecordParser parser;
    Entity entity = makeEntity({ makeField("blob", FieldType::Binary) });

    verifyThrows<RuntimeError>([&]() {
        [[maybe_unused]] auto r = parser.parse(entity, R"({"blob": 12345})");
    });
}

// ═════════════════════════════════════════════════════════════
// 5c. Dépassement de plage des entiers
//
// Le parser lit la valeur en int64_t puis vérifie qu'elle tient
// dans le type cible AVANT conversion. Une valeur hors plage, ou
// négative sur un type non signé, lève une RUNTIME_EXECUTION.
// ═════════════════════════════════════════════════════════════

void TestJsonRecordParser::parse_smallIntOutOfRange_shouldThrow() {
    // 100000 dépasse la borne haute d'un int16_t (32767).
    JsonRecordParser parser;
    Entity entity = makeEntity({ makeField("level", FieldType::SmallInt) });

    verifyThrows<RuntimeError>([&]() {
        [[maybe_unused]] auto r = parser.parse(entity, R"({"level": 100000})");
    });
}

void TestJsonRecordParser::parse_smallIntAtUpperBound_shouldPass() {
    // 32767 est exactement la borne haute d'un int16_t : accepté.
    JsonRecordParser parser;
    Entity entity = makeEntity({ makeField("level", FieldType::SmallInt) });

    const auto record = parser.parse(entity, R"({"level": 32767})");

    QVERIFY(std::holds_alternative<std::int16_t>(record.at("level")));
    QCOMPARE(std::get<std::int16_t>(record.at("level")),
             std::int16_t(32767));
}

void TestJsonRecordParser::parse_unsignedSmallIntNegative_shouldThrow() {
    // Une valeur négative sur un SmallInt non signé est rejetée.
    JsonRecordParser parser;
    Entity entity = makeEntity({
        makeField("level", FieldType::SmallInt, /*unsignedValue=*/true),
    });

    verifyThrows<RuntimeError>([&]() {
        [[maybe_unused]] auto r = parser.parse(entity, R"({"level": -5})");
    });
}

void TestJsonRecordParser::parse_unsignedSmallIntOutOfRange_shouldThrow() {
    // 70000 dépasse la borne haute d'un uint16_t (65535).
    JsonRecordParser parser;
    Entity entity = makeEntity({
        makeField("level", FieldType::SmallInt, /*unsignedValue=*/true),
    });

    verifyThrows<RuntimeError>([&]() {
        [[maybe_unused]] auto r = parser.parse(entity, R"({"level": 70000})");
    });
}

void TestJsonRecordParser::parse_intOutOfRange_shouldThrow() {
    // 5000000000 dépasse la borne haute d'un int32_t (2147483647).
    JsonRecordParser parser;
    Entity entity = makeEntity({ makeField("count", FieldType::Int) });

    verifyThrows<RuntimeError>([&]() {
        [[maybe_unused]] auto r = parser.parse(entity, R"({"count": 5000000000})");
    });
}

void TestJsonRecordParser::parse_intAtUpperBound_shouldPass() {
    // 2147483647 est exactement la borne haute d'un int32_t : accepté.
    JsonRecordParser parser;
    Entity entity = makeEntity({ makeField("count", FieldType::Int) });

    const auto record = parser.parse(entity, R"({"count": 2147483647})");

    QVERIFY(std::holds_alternative<std::int32_t>(record.at("count")));
    QCOMPARE(std::get<std::int32_t>(record.at("count")),
             std::int32_t(2147483647));
}

void TestJsonRecordParser::parse_unsignedIntNegative_shouldThrow() {
    // Une valeur négative sur un Int non signé est rejetée.
    JsonRecordParser parser;
    Entity entity = makeEntity({
        makeField("count", FieldType::Int, /*unsignedValue=*/true),
    });

    verifyThrows<RuntimeError>([&]() {
        [[maybe_unused]] auto r = parser.parse(entity, R"({"count": -1})");
    });
}

void TestJsonRecordParser::parse_unsignedBigIntNegative_shouldThrow() {
    // Une valeur négative sur un BigInt non signé est rejetée.
    JsonRecordParser parser;
    Entity entity = makeEntity({
        makeField("big", FieldType::BigInt, /*unsignedValue=*/true),
    });

    verifyThrows<RuntimeError>([&]() {
        [[maybe_unused]] auto r = parser.parse(entity, R"({"big": -1})");
    });
}

// ═════════════════════════════════════════════════════════════
// 6. Champs mal typés -> exception (parsing strict)
//
// Le parser ne masque PLUS un champ mal typé : dès qu'un champ
// dont le type JSON ne correspond pas au FieldType déclaré est
// rencontré, parse() lève une RUNTIME_EXECUTION et tout le
// parsing échoue. Les handlers HTTP traduisent ça en 400.
// ═════════════════════════════════════════════════════════════

void TestJsonRecordParser::parse_stringFieldWithIntValue_shouldThrow() {
    JsonRecordParser parser;
    Entity entity = makeEntity({ makeField("name", FieldType::String) });

    verifyThrows<RuntimeError>([&]() {
        [[maybe_unused]] auto r = parser.parse(entity, R"({"name": 123})");
    });
}

void TestJsonRecordParser::parse_intFieldWithStringValue_shouldThrow() {
    JsonRecordParser parser;
    Entity entity = makeEntity({ makeField("age", FieldType::Int) });

    verifyThrows<RuntimeError>([&]() {
        [[maybe_unused]] auto r = parser.parse(entity, R"({"age": "not a number"})");
    });
}

void TestJsonRecordParser::parse_boolFieldWithStringValue_shouldThrow() {
    JsonRecordParser parser;
    Entity entity = makeEntity({ makeField("active", FieldType::Bool) });

    verifyThrows<RuntimeError>([&]() {
        [[maybe_unused]] auto r = parser.parse(entity, R"({"active": "true"})");
    });
}

void TestJsonRecordParser::parse_floatFieldWithStringValue_shouldThrow() {
    // Un champ Float attend un nombre : une chaîne est rejetée.
    JsonRecordParser parser;
    Entity entity = makeEntity({ makeField("score", FieldType::Float) });

    verifyThrows<RuntimeError>([&]() {
        [[maybe_unused]] auto r = parser.parse(entity, R"({"score": "high"})");
    });
}

void TestJsonRecordParser::parse_invalidFieldAbortsWholeParse() {
    // Un seul champ mal typé fait échouer l'intégralité du parsing :
    // le parser ne renvoie pas un record partiel avec les champs
    // valides — il lève.
    JsonRecordParser parser;
    Entity entity = makeEntity({
        makeField("name",   FieldType::String),
        makeField("age",    FieldType::Int),
        makeField("active", FieldType::Bool),
    });

    verifyThrows<RuntimeError>([&]() {
        // "age" mal typé : tout le parse échoue, même si "name" et
        // "active" sont corrects.
        [[maybe_unused]] auto r = parser.parse(
            entity, R"({"name": "Alice", "age": "oops", "active": true})");
    });
}