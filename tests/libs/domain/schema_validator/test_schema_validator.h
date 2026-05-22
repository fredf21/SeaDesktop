#ifndef TEST_SCHEMA_VALIDATOR_H
#define TEST_SCHEMA_VALIDATOR_H

class test_schema_validator
{
public:
    test_schema_validator();
};

#endif // TEST_SCHEMA_VALIDATOR_H
#pragma once

#include <QObject>

class TestSchemaValidator : public QObject
{
    Q_OBJECT

private slots:
    void validate_emptySchema_shouldReportError();

    void validate_validSimpleSchema_shouldPass();

    void validate_emptyEntityName_shouldReportError();
    void validate_invalidEntityName_shouldReportError();
    void validate_duplicateEntityName_shouldReportError();
    void validate_entityWithoutFields_shouldReportError();

    void validate_emptyFieldName_shouldReportError();
    void validate_invalidFieldName_shouldReportError();
    void validate_duplicateFieldName_shouldReportError();

    void validate_passwordSerializable_shouldReportError();
    void validate_passwordWithDefault_shouldReportError();
    void validate_binaryWithDefault_shouldReportError();

    void validate_maxLengthOnString_shouldPass();
    void validate_maxLengthOnInt_shouldReportError();
    void validate_maxLengthZero_shouldReportError();

    void validate_unsignedValueOnInt_shouldPass();
    void validate_unsignedValueOnString_shouldReportError();

    void validate_minGreaterThanMax_shouldReportError();

    void validate_relationUnknownTarget_shouldReportError();
    void validate_manyToManyWithoutPivotTable_shouldReportError();
    void validate_belongsToWithoutFkColumn_shouldReportError();

    void validate_fileWithoutConfig_shouldReportError();
    void validate_fileWithValidConfig_shouldPass();
    void validate_fileWithAbsoluteStoragePath_shouldReportError();
    void validate_fileWithPathTraversal_shouldReportError();
    void validate_fileWithDuplicateStoragePath_shouldReportError();

    void validate_paginationWithoutMode_shouldReportError();
    void validate_pagePaginationDefaultGreaterThanMax_shouldReportError();
    void validate_pagePaginationUnknownSortableField_shouldReportError();
    void validate_pagePaginationMalformedDefaultSort_shouldReportError();
    void validate_cursorPaginationUnknownCursorField_shouldReportError();

    void validate_pagePaginationValidConfig_shouldPass();
    void validate_pagePaginationDefaultSizeZero_shouldReportError();
    void validate_offsetPaginationValidConfig_shouldPass();
    void validate_offsetPaginationDefaultGreaterThanMax_shouldReportError();
    void validate_cursorPaginationValidConfig_shouldPass();
    void validate_cursorPaginationMissingSort_shouldReportError();

};