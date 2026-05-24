#pragma once

#include <QObject>

// ─────────────────────────────────────────────────────────────
// TestSchemaValidator
//
// Suite de tests du SchemaValidator : vérifie la cohérence métier
// d'un Schema. validate() ne lève jamais — il retourne un
// vector<string> d'erreurs (vide == schéma valide).
//
// Organisation :
//   1. Schéma / entités (vide, noms, doublons, sans champ)
//   2. Champs (noms, doublons, password, binary, max_length,
//      unsigned_value, min/max)
//   3. Champs File (config, contraintes interdites, storage_path,
//      max_size, collisions)
//   4. Relations (noms, doublons, target, M2M, belongs_to)
//   5. Pagination (page / offset / cursor, sort)
//   6. Accumulation d'erreurs
// ─────────────────────────────────────────────────────────────
class TestSchemaValidator : public QObject
{
    Q_OBJECT

private slots:
    // ── 1. Schéma / entités ───────────────────────────────────
    void validate_emptySchema_shouldReportError();
    void validate_validSimpleSchema_shouldPass();
    void validate_emptyEntityName_shouldReportError();
    void validate_invalidEntityName_shouldReportError();
    void validate_duplicateEntityName_shouldReportError();
    void validate_entityWithoutFields_shouldReportError();

    // ── 2. Champs ─────────────────────────────────────────────
    void validate_emptyFieldName_shouldReportError();
    void validate_invalidFieldName_shouldReportError();
    void validate_duplicateFieldName_shouldReportError();

    void validate_passwordSerializable_shouldReportError();
    void validate_passwordWithDefault_shouldReportError();
    void validate_binaryWithDefault_shouldReportError();

    void validate_maxLengthOnString_shouldPass();
    void validate_maxLengthOnText_shouldPass();
    void validate_maxLengthOnInt_shouldReportError();
    void validate_maxLengthZero_shouldReportError();

    void validate_unsignedValueOnInt_shouldPass();
    void validate_unsignedValueOnFloat_shouldPass();
    void validate_unsignedValueOnString_shouldReportError();

    void validate_minGreaterThanMax_shouldReportError();
    void validate_minGreaterThanMaxDouble_shouldReportError();
    void validate_minEqualsMax_shouldPass();

    // ── 3. Champs File ────────────────────────────────────────
    void validate_fileWithoutConfig_shouldReportError();
    void validate_fileWithValidConfig_shouldPass();
    void validate_fileWithAbsoluteStoragePath_shouldReportError();
    void validate_fileWithPathTraversal_shouldReportError();
    void validate_fileWithDuplicateStoragePath_shouldReportError();
    void validate_fileWithEmptyStoragePath_shouldReportError();
    void validate_fileWithEmptyPathSegment_shouldReportError();
    void validate_fileUnique_shouldReportError();
    void validate_fileIndexed_shouldReportError();
    void validate_fileWithMaxLength_shouldReportError();
    void validate_fileWithMinMaxValue_shouldReportError();
    void validate_fileWithZeroMaxSize_shouldReportError();
    void validate_fileWithHugeMaxSize_shouldReportWarning();

    // ── 4. Relations ──────────────────────────────────────────
    void validate_relationUnnamed_shouldReportError();
    void validate_relationDuplicate_shouldReportError();
    void validate_relationWithoutTarget_shouldReportError();
    void validate_relationUnknownTarget_shouldReportError();
    void validate_manyToManyWithoutPivotTable_shouldReportError();
    void validate_belongsToWithoutFkColumn_shouldReportError();

    // ── 5. Pagination ─────────────────────────────────────────
    void validate_paginationWithoutMode_shouldReportError();

    void validate_pagePaginationValidConfig_shouldPass();
    void validate_pagePaginationDefaultGreaterThanMax_shouldReportError();
    void validate_pagePaginationDefaultSizeZero_shouldReportError();
    void validate_pagePaginationMaxSizeZero_shouldReportError();
    void validate_pagePaginationUnknownSortableField_shouldReportError();
    void validate_pagePaginationMalformedDefaultSort_shouldReportError();
    void validate_pagePaginationSortFieldNotWhitelisted_shouldReportError();

    void validate_offsetPaginationValidConfig_shouldPass();
    void validate_offsetPaginationDefaultGreaterThanMax_shouldReportError();
    void validate_offsetPaginationDefaultLimitZero_shouldReportError();
    void validate_offsetPaginationUnknownSortableField_shouldReportError();

    void validate_cursorPaginationValidConfig_shouldPass();
    void validate_cursorPaginationMissingSort_shouldReportError();
    void validate_cursorPaginationUnknownCursorField_shouldReportError();
    void validate_cursorPaginationEmptyCursorField_shouldReportError();
    void validate_cursorPaginationMalformedSort_shouldReportError();
    void validate_cursorPaginationDefaultGreaterThanMax_shouldReportError();
    void validate_cursorPaginationMultiSort_shouldPass();

    // ── 6. Accumulation d'erreurs ─────────────────────────────
    void validate_multipleErrors_shouldAllBeReported();
};