#include "test_mysql_type_mapping.h"
#include "database_mappings/mysql_type_mapping.h"
#include "field_type.h"
#include <qtestcase.h>

namespace {

QString qs(std::string_view value)
{
    return QString::fromStdString(std::string(value));
}

} // namespace
void TestMysqlTypeMapping::toMysqlType_shouldReturnExpectedBasicTypes()
{
    using namespace sea::domain;

    QCOMPARE(qs(to_mysql_type(FieldType::String)), QString("VARCHAR(255)"));
    QCOMPARE(qs(to_mysql_type(FieldType::Text)), QString("TEXT"));
    QCOMPARE(qs(to_mysql_type(FieldType::Int)), QString("BIGINT"));
    QCOMPARE(qs(to_mysql_type(FieldType::Float)), QString("DOUBLE"));
    QCOMPARE(qs(to_mysql_type(FieldType::Bool)), QString("BOOLEAN"));
    QCOMPARE(qs(to_mysql_type(FieldType::Timestamp)), QString("TIMESTAMP"));
    QCOMPARE(qs(to_mysql_type(FieldType::UUID)), QString("BINARY(16)"));
    QCOMPARE(qs(to_mysql_type(FieldType::Password)), QString("VARCHAR(255)"));
    QCOMPARE(qs(to_mysql_type(FieldType::Email)), QString("VARCHAR(255)"));
    QCOMPARE(qs(to_mysql_type(FieldType::File)), QString("BINARY(16)"));
}

void TestMysqlTypeMapping::autoIncrement_shouldOnlySupportInt()
{
    using namespace sea::domain;

    QVERIFY(mysql_supports_auto_increment(FieldType::Int));

    QVERIFY(!mysql_supports_auto_increment(FieldType::UUID));
    QVERIFY(!mysql_supports_auto_increment(FieldType::BigInt));
    QVERIFY(!mysql_supports_auto_increment(FieldType::String));
    QVERIFY(!mysql_supports_auto_increment(FieldType::File));
}

void TestMysqlTypeMapping::binaryStorage_shouldSupportUuidAndFile()
{
    using namespace sea::domain;

    QVERIFY(mysql_uses_binary_storage(FieldType::UUID));
    QVERIFY(mysql_uses_binary_storage(FieldType::File));

    QVERIFY(!mysql_uses_binary_storage(FieldType::String));
    QVERIFY(!mysql_uses_binary_storage(FieldType::Int));
    QVERIFY(!mysql_uses_binary_storage(FieldType::Email));
}