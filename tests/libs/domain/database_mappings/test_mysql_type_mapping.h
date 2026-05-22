#pragma once

#include <QObject>

class TestMysqlTypeMapping : public QObject
{
    Q_OBJECT

private slots:
    void toMysqlType_shouldReturnExpectedBasicTypes();
    void autoIncrement_shouldOnlySupportInt();
    void binaryStorage_shouldSupportUuidAndFile();
};