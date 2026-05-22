#pragma once

#include <QObject>

class TestSecuritySchemeConfig : public QObject
{
    Q_OBJECT

private slots:
    // AbacMode
    void abacMode_fromString_shouldParseValidValues();
    void abacMode_fromString_invalidShouldReturnNullopt();
    void abacMode_toString_shouldReturnExpectedValues();

    // CookieConfig
    void cookieConfig_tokenDeliveryFromString_shouldParseValidValues();
    void cookieConfig_sameSiteFromString_shouldParseValidValues();
    void cookieConfig_safeDefaults_shouldBeValid();
    void cookieConfig_sameSiteNoneWithoutSecure_shouldThrow();
    void cookieConfig_emptyPath_shouldThrow();
    void cookieConfig_sameTokenNames_shouldThrow();

    // AuthentificationConfig
    void authentication_defaultShouldBeDisabled();
    void authentication_authTypeFromString_shouldParseValidValues();
    void authentication_jwtAlgorithmFromString_shouldParseValidValues();
    void authentication_jwtWithValidSecret_shouldPass();
    void authentication_jwtWithShortSecret_shouldThrow();
    void authentication_jwtAsymmetricWithoutPublicKey_shouldThrow();
    void authentication_apiKeyWithEmptyHeader_shouldThrow();
    void authentication_oauth2MissingUrls_shouldThrow();

    // TokenTrackingConfig
    void tokenTracking_disabled_shouldSkipValidation();
    void tokenTracking_enabledWithDefaults_shouldBeValid();
    void tokenTracking_sameTables_shouldThrow();
    void tokenTracking_cacheEnabledWithZeroSize_shouldThrow();
    void tokenTracking_autoCleanupInvalidInterval_shouldThrow();

    // CorsConfig
    void cors_denyAll_shouldBeDisabled();
    void cors_allowAll_shouldUseWildcardWithoutCredentials();
    void cors_permissive_shouldAllowConfiguredOrigins();
    void cors_credentialsWithWildcard_shouldThrow();
    void cors_enabledWithoutMethods_shouldThrow();
    void cors_negativeMaxAge_shouldThrow();

    // HttpLimits
    void httpLimits_safeDefaults_shouldBeValid();
    void httpLimits_zeroBodySize_shouldThrow();
    void httpLimits_invalidTimeout_shouldThrow();
    void httpLimits_zeroHeadersCount_shouldThrow();

    // RateLimitRule
    void rateLimitScope_fromString_shouldParseValidValues();
    void rateLimitRule_validRule_shouldPass();
    void rateLimitRule_zeroRequests_shouldThrow();
    void rateLimitRule_burstLowerThanRequests_shouldThrow();
    void rateLimitRule_refillRate_shouldComputeRequestsPerSecond();

    // SecurityHeaders
    void securityHeaders_none_shouldDisableAllHeaders();
    void securityHeaders_recommended_shouldSetCommonHeaders();
    void securityHeaders_strict_shouldSetCrossOriginHeaders();
    void securityHeaders_disableHeader_shouldClearValue();

    // SecurityConfig
    void securityConfig_safeDefaults_shouldBeValid();
    void securityConfig_disabled_shouldBeValid();
    void securityConfig_invalidRateLimit_shouldThrow();
    void securityConfig_corsWildcardWithCredentials_shouldThrow();
};