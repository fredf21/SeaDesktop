#include "test_mysql_type_mapping.h"
#include "database_mappings/mysql_type_mapping.h"
#include "field_type.h"

#include <QtTest>

#include <string>
#include <string_view>

namespace {

QString qs(std::string_view value)
{
    return QString::fromStdString(std::string(value));
}

} // namespace

// ═════════════════════════════════════════════════════════════
// to_mysql_type
// ═════════════════════════════════════════════════════════════

void TestMysqlTypeMapping::toMysqlType_shouldReturnExpectedBasicTypes()
{
    using namespace sea::domain;

    // Types ayant un mapping MySQL dédié dans le switch.
    QCOMPARE(qs(to_mysql_type(FieldType::String)),    QString("VARCHAR(255)"));
    QCOMPARE(qs(to_mysql_type(FieldType::Text)),      QString("TEXT"));
    QCOMPARE(qs(to_mysql_type(FieldType::Int)),       QString("BIGINT"));
    QCOMPARE(qs(to_mysql_type(FieldType::Float)),     QString("DOUBLE"));
    QCOMPARE(qs(to_mysql_type(FieldType::Bool)),      QString("BOOLEAN"));
    QCOMPARE(qs(to_mysql_type(FieldType::Timestamp)), QString("TIMESTAMP"));
    QCOMPARE(qs(to_mysql_type(FieldType::UUID)),      QString("BINARY(16)"));
    QCOMPARE(qs(to_mysql_type(FieldType::Password)),  QString("VARCHAR(255)"));
    QCOMPARE(qs(to_mysql_type(FieldType::Email)),     QString("VARCHAR(255)"));
    QCOMPARE(qs(to_mysql_type(FieldType::File)),      QString("BINARY(16)"));
}

void TestMysqlTypeMapping::toMysqlType_fallbackTypes_shouldDefaultToVarchar()
{
    using namespace sea::domain;

    // Ces FieldType n'ont PAS de case dédié dans to_mysql_type :
    // ils retombent sur le default (VARCHAR(255)). Ce test fige ce
    // comportement — si un mapping dédié est ajouté un jour pour
    // l'un d'eux, ce test échouera et signalera la régression.
    QCOMPARE(qs(to_mysql_type(FieldType::BigInt)),   QString("VARCHAR(255)"));
    QCOMPARE(qs(to_mysql_type(FieldType::SmallInt)), QString("VARCHAR(255)"));
    QCOMPARE(qs(to_mysql_type(FieldType::Decimal)),  QString("VARCHAR(255)"));
    QCOMPARE(qs(to_mysql_type(FieldType::Json)),     QString("VARCHAR(255)"));
    QCOMPARE(qs(to_mysql_type(FieldType::Binary)),   QString("VARCHAR(255)"));
    QCOMPARE(qs(to_mysql_type(FieldType::Native)),   QString("VARCHAR(255)"));
}

// ═════════════════════════════════════════════════════════════
// mysql_supports_auto_increment
// ═════════════════════════════════════════════════════════════

void TestMysqlTypeMapping::autoIncrement_shouldOnlySupportInt()
{
    using namespace sea::domain;

    // Seul Int supporte AUTO_INCREMENT.
    QVERIFY(mysql_supports_auto_increment(FieldType::Int));

    // Tous les autres types : non.
    QVERIFY(!mysql_supports_auto_increment(FieldType::String));
    QVERIFY(!mysql_supports_auto_increment(FieldType::Text));
    QVERIFY(!mysql_supports_auto_increment(FieldType::Float));
    QVERIFY(!mysql_supports_auto_increment(FieldType::Bool));
    QVERIFY(!mysql_supports_auto_increment(FieldType::Timestamp));
    QVERIFY(!mysql_supports_auto_increment(FieldType::UUID));
    QVERIFY(!mysql_supports_auto_increment(FieldType::BigInt));
    QVERIFY(!mysql_supports_auto_increment(FieldType::SmallInt));
    QVERIFY(!mysql_supports_auto_increment(FieldType::Decimal));
    QVERIFY(!mysql_supports_auto_increment(FieldType::Json));
    QVERIFY(!mysql_supports_auto_increment(FieldType::Binary));
    QVERIFY(!mysql_supports_auto_increment(FieldType::Password));
    QVERIFY(!mysql_supports_auto_increment(FieldType::Email));
    QVERIFY(!mysql_supports_auto_increment(FieldType::File));
    QVERIFY(!mysql_supports_auto_increment(FieldType::Native));
}

// ═════════════════════════════════════════════════════════════
// mysql_uses_binary_storage
// ═════════════════════════════════════════════════════════════

void TestMysqlTypeMapping::binaryStorage_shouldSupportUuidAndFile()
{
    using namespace sea::domain;

    // UUID et File sont stockés en BINARY(16).
    QVERIFY(mysql_uses_binary_storage(FieldType::UUID));
    QVERIFY(mysql_uses_binary_storage(FieldType::File));

    // Tous les autres types ne sont pas en stockage binaire.
    QVERIFY(!mysql_uses_binary_storage(FieldType::String));
    QVERIFY(!mysql_uses_binary_storage(FieldType::Text));
    QVERIFY(!mysql_uses_binary_storage(FieldType::Int));
    QVERIFY(!mysql_uses_binary_storage(FieldType::Float));
    QVERIFY(!mysql_uses_binary_storage(FieldType::Bool));
    QVERIFY(!mysql_uses_binary_storage(FieldType::Timestamp));
    QVERIFY(!mysql_uses_binary_storage(FieldType::BigInt));
    QVERIFY(!mysql_uses_binary_storage(FieldType::SmallInt));
    QVERIFY(!mysql_uses_binary_storage(FieldType::Decimal));
    QVERIFY(!mysql_uses_binary_storage(FieldType::Json));
    QVERIFY(!mysql_uses_binary_storage(FieldType::Binary));
    QVERIFY(!mysql_uses_binary_storage(FieldType::Password));
    QVERIFY(!mysql_uses_binary_storage(FieldType::Email));
    QVERIFY(!mysql_uses_binary_storage(FieldType::Native));
}