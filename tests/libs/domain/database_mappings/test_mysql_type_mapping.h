#pragma once

#include <QObject>

// ─────────────────────────────────────────────────────────────
// TestMysqlTypeMapping
//
// Vérifie le mapping FieldType -> type MySQL utilisé par le
// générateur de schéma (to_mysql_type) ainsi que les deux helpers
// mysql_supports_auto_increment et mysql_uses_binary_storage.
//
// Le mapping a un cas default (VARCHAR(255)) : les tests couvrent
// AUSSI les types qui retombent sur ce default, pour qu'une
// évolution future du switch soit détectée par un test.
// ─────────────────────────────────────────────────────────────
class TestMysqlTypeMapping : public QObject
{
    Q_OBJECT

private slots:
    // to_mysql_type
    void toMysqlType_shouldReturnExpectedBasicTypes();
    void toMysqlType_fallbackTypes_shouldDefaultToVarchar();

    // mysql_supports_auto_increment
    void autoIncrement_shouldOnlySupportInt();

    // mysql_uses_binary_storage
    void binaryStorage_shouldSupportUuidAndFile();
};