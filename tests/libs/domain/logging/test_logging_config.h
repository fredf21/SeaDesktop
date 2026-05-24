#pragma once

#include <QObject>

class TestLoggingConfig : public QObject
{
    Q_OBJECT

private slots:
    void logLevelFromString_shouldSupportAliases();
    void logLevelFromString_invalidShouldThrow();

    void logFormatFromString_shouldSupportAliases();

    void sinkTypeFromString_shouldSupportAliases();

    void timePatternFromString_shouldParseValues();

    void loggingConfig_safeDefaults_shouldBeValid();
    void loggingConfig_disabled_shouldSkipValidation();
    void loggingConfig_effectiveLevel_shouldUseExactOverride();
    void loggingConfig_effectiveLevel_shouldUseLongestPrefixOverride();
    void loggingConfig_effectiveLevel_partialNameIsNotAPrefix();
    void loggingConfig_effectiveLevel_noOverride_shouldUseGlobalLevel();
    void loggingConfig_fileSinkWithoutPath_shouldThrow();
    void loggingConfig_allSinksDisabled_shouldThrow();
    void loggingConfig_asyncQueueSizeZero_shouldThrow();
    void loggingConfig_asyncDisabledWithZeroQueue_shouldNotThrow();
    void loggingConfig_fileSinkMaxFilesZero_shouldThrow();
    void loggingConfig_validFileSink_shouldPass();
    void loggingConfig_disabledFileSinkWithoutPath_shouldBeIgnored();

    // builder fluide
    void loggingConfig_builder_addSink_shouldAppend();
    void loggingConfig_builder_setFlushAndEnabled_shouldApply();

    // RotationConfig helpers
    void rotationConfig_sizeRotationEnabled_dependsOnMaxSize();
    void rotationConfig_timeRotationEnabled_dependsOnPattern();
};