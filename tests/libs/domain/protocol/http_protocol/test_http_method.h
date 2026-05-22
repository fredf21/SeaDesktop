#pragma once

#include <QObject>

class TestHttpMethod : public QObject
{
    Q_OBJECT

private slots:
    void toString_shouldReturnUppercaseMethods();
    void fromString_shouldParseValidMethods();
    void fromString_invalidShouldThrow();
    void fromString_lowercaseShouldThrow();
};