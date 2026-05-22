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
    void loggingConfig_fileSinkWithoutPath_shouldThrow();
    void loggingConfig_allSinksDisabled_shouldThrow();
    void loggingConfig_asyncQueueSizeZero_shouldThrow();
};