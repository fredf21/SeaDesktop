#pragma once

#include <QObject>

class TestOperatorStrategies : public QObject
{
    Q_OBJECT

private slots:
    // NumericHelper
    void numericHelper_parseValidNumber_shouldReturnDouble();
    void numericHelper_parseInvalidNumber_shouldReturnNullopt();
    void numericHelper_parsePartialNumber_shouldReturnNullopt();
    void numericHelper_parseBothScalars_shouldReturnPair();
    void numericHelper_parseBothWithNonScalar_shouldReturnNullopt();

    // EqualsStrategy
    void equals_scalarEqual_shouldReturnTrue();
    void equals_scalarDifferent_shouldReturnFalse();
    void equals_listEqualSameOrder_shouldReturnTrue();
    void equals_listDifferentOrder_shouldReturnFalse();
    void equals_emptyValues_shouldReturnTrue();
    void equals_mixedTypes_shouldReturnFalse();

    // ExistsStrategy
    void exists_scalar_shouldReturnTrue();
    void exists_list_shouldReturnTrue();
    void exists_empty_shouldReturnFalse();
    void exists_shouldIgnoreRightOperand();

    // ContainsStrategy
    void contains_listContainsScalar_shouldReturnTrue();
    void contains_listDoesNotContainScalar_shouldReturnFalse();
    void contains_stringContainsSubstring_shouldReturnTrue();
    void contains_stringDoesNotContainSubstring_shouldReturnFalse();
    void contains_invalidTypes_shouldReturnFalse();

    // InStrategy
    void in_scalarInList_shouldReturnTrue();
    void in_scalarNotInList_shouldReturnFalse();
    void in_invalidTypes_shouldReturnFalse();

    // IntersectsStrategy
    void intersects_listsWithCommonValue_shouldReturnTrue();
    void intersects_listsWithoutCommonValue_shouldReturnFalse();
    void intersects_scalarInList_shouldReturnTrue();
    void intersects_scalarNotInList_shouldReturnFalse();
    void intersects_invalidTypes_shouldReturnFalse();

    // EndsWithStrategy
    void endsWith_validSuffix_shouldReturnTrue();
    void endsWith_invalidSuffix_shouldReturnFalse();
    void endsWith_suffixLongerThanText_shouldReturnFalse();
    void endsWith_invalidTypes_shouldReturnFalse();

    // GreaterThanStrategy
    void greaterThan_leftGreater_shouldReturnTrue();
    void greaterThan_leftEqual_shouldReturnFalse();
    void greaterThan_invalidNumber_shouldReturnFalse();

    // GreaterThanOrEqualStrategy
    void greaterThanOrEqual_leftGreater_shouldReturnTrue();
    void greaterThanOrEqual_leftEqual_shouldReturnTrue();
    void greaterThanOrEqual_leftSmaller_shouldReturnFalse();
    void greaterThanOrEqual_invalidNumber_shouldReturnFalse();

    // Names
    void strategyNames_shouldReturnExpectedNames();

    // NotEqualsStrategy
    void notEquals_scalarDifferent_shouldReturnTrue();
    void notEquals_scalarEqual_shouldReturnFalse();
    void notEquals_listDifferent_shouldReturnTrue();
    void notEquals_emptyValues_shouldReturnFalse();
    void notEquals_mixedTypes_shouldReturnTrue();

    // NotExistsStrategy
    void notExists_empty_shouldReturnTrue();
    void notExists_scalar_shouldReturnFalse();
    void notExists_list_shouldReturnFalse();
    void notExists_shouldIgnoreRightOperand();

    // NotInStrategy
    void notIn_scalarNotInList_shouldReturnTrue();
    void notIn_scalarInList_shouldReturnFalse();
    void notIn_invalidTypes_shouldReturnTrue();

    // LessThanStrategy
    void lessThan_leftSmaller_shouldReturnTrue();
    void lessThan_leftEqual_shouldReturnFalse();
    void lessThan_leftGreater_shouldReturnFalse();
    void lessThan_invalidNumber_shouldReturnFalse();

    // LessThanOrEqualStrategy
    void lessThanOrEqual_leftSmaller_shouldReturnTrue();
    void lessThanOrEqual_leftEqual_shouldReturnTrue();
    void lessThanOrEqual_leftGreater_shouldReturnFalse();
    void lessThanOrEqual_invalidNumber_shouldReturnFalse();

    // StartsWithStrategy
    void startsWith_validPrefix_shouldReturnTrue();
    void startsWith_invalidPrefix_shouldReturnFalse();
    void startsWith_prefixLongerThanText_shouldReturnFalse();
    void startsWith_invalidTypes_shouldReturnFalse();

    // RegexMatchStrategy
    void regexMatch_cachedPatternShouldMatch();
    void regexMatch_cachedPatternShouldNotMatch();
    void regexMatch_missingPatternShouldReturnFalse();
    void regexMatch_invalidTypesShouldReturnFalse();

    // OperatorRegistry
    void operatorRegistry_defaultShouldContainAllStandardOperators();
    void operatorRegistry_findUnknownOrMissingShouldReturnNullptr();
    void operatorRegistry_regexMatchShouldUseProvidedCache();
};