#include "test_security_scheme_config.h"

#include "security_scheme/abac_mode.h"
#include "security_scheme/authentification_config.h"
#include "security_scheme/cookie_config.h"
#include "security_scheme/cors_config.h"
#include "security_scheme/http_limit.h"
#include "security_scheme/rate_limit_rule.h"
#include "security_scheme/security_config.h"
#include "security_scheme/security_headers.h"
#include "security_scheme/token_tracking_config.h"

#include <QtTest>

#include <chrono>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace sea::domain::access_control;
using namespace sea::domain::security;

namespace {

QString qs(std::string_view value)
{
    return QString::fromStdString(std::string(value));
}

std::string strongSecret()
{
    return "0123456789abcdef0123456789abcdef";
}

} // namespace

// ─────────────────────────────────────────────
// AbacMode
// ─────────────────────────────────────────────

void TestSecuritySchemeConfig::abacMode_fromString_shouldParseValidValues()
{
    QCOMPARE(abac_mode_from_string("permissive").value(), AbacMode::Permissive);
    QCOMPARE(abac_mode_from_string("PERMISSIVE").value(), AbacMode::Permissive);
    QCOMPARE(abac_mode_from_string("strict").value(), AbacMode::Strict);
    QCOMPARE(abac_mode_from_string("STRICT").value(), AbacMode::Strict);
}

void TestSecuritySchemeConfig::abacMode_fromString_invalidShouldReturnNullopt()
{
    QVERIFY(!abac_mode_from_string("audit").has_value());
}

void TestSecuritySchemeConfig::abacMode_toString_shouldReturnExpectedValues()
{
    QCOMPARE(qs(to_string(AbacMode::Permissive)), QString("permissive"));
    QCOMPARE(qs(to_string(AbacMode::Strict)), QString("strict"));
}

// ─────────────────────────────────────────────
// CookieConfig
// ─────────────────────────────────────────────

void TestSecuritySchemeConfig::cookieConfig_tokenDeliveryFromString_shouldParseValidValues()
{
    QCOMPARE(token_delivery_from_string("body"), TokenDelivery::Body);
    QCOMPARE(token_delivery_from_string("cookie"), TokenDelivery::Cookie);
    QCOMPARE(token_delivery_from_string("both"), TokenDelivery::Both);

    QCOMPARE(qs(to_string(TokenDelivery::Body)), QString("body"));
    QCOMPARE(qs(to_string(TokenDelivery::Cookie)), QString("cookie"));
    QCOMPARE(qs(to_string(TokenDelivery::Both)), QString("both"));

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, token_delivery_from_string("header"));
}

void TestSecuritySchemeConfig::cookieConfig_sameSiteFromString_shouldParseValidValues()
{
    QCOMPARE(same_site_from_string("lax"), SameSitePolicy::Lax);
    QCOMPARE(same_site_from_string("Strict"), SameSitePolicy::Strict);
    QCOMPARE(same_site_from_string("None"), SameSitePolicy::None);

    QCOMPARE(qs(to_string(SameSitePolicy::Lax)), QString("Lax"));
    QCOMPARE(qs(to_string(SameSitePolicy::Strict)), QString("Strict"));
    QCOMPARE(qs(to_string(SameSitePolicy::None)), QString("None"));

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, same_site_from_string("invalid"));
}

void TestSecuritySchemeConfig::cookieConfig_safeDefaults_shouldBeValid()
{
    auto cfg = CookieConfig::safe_defaults();

    QCOMPARE(QString::fromStdString(cfg.path()), QString("/"));
    QVERIFY(cfg.secure());
    QCOMPARE(cfg.same_site(), SameSitePolicy::Lax);
    QCOMPARE(QString::fromStdString(cfg.access_token_name()), QString("sea_access"));
    QCOMPARE(QString::fromStdString(cfg.refresh_token_name()), QString("sea_refresh"));
    QVERIFY(cfg.http_only());

    QVERIFY_THROWS_NO_EXCEPTION(cfg.validate());
}

void TestSecuritySchemeConfig::cookieConfig_sameSiteNoneWithoutSecure_shouldThrow()
{
    auto cfg = CookieConfig::safe_defaults();
    cfg.set_same_site(SameSitePolicy::None);
    cfg.set_secure(false);

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cfg.validate());
}

void TestSecuritySchemeConfig::cookieConfig_emptyPath_shouldThrow()
{
    auto cfg = CookieConfig::safe_defaults();
    cfg.set_path("");

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cfg.validate());
}

void TestSecuritySchemeConfig::cookieConfig_sameTokenNames_shouldThrow()
{
    auto cfg = CookieConfig::safe_defaults();
    cfg.set_access_token_name("token");
    cfg.set_refresh_token_name("token");

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cfg.validate());
}

// ─────────────────────────────────────────────
// AuthentificationConfig
// ─────────────────────────────────────────────

void TestSecuritySchemeConfig::authentication_defaultShouldBeDisabled()
{
    AuthentificationConfig cfg;

    QCOMPARE(cfg.type(), AuthType::None);
    QVERIFY(!cfg.is_enabled());
    QVERIFY(!cfg.uses_symmetric_key());
    QVERIFY(!cfg.uses_asymmetric_key());

    QVERIFY_THROWS_NO_EXCEPTION(cfg.validate());
}

void TestSecuritySchemeConfig::authentication_authTypeFromString_shouldParseValidValues()
{
    QCOMPARE(auth_type_from_string("none"), AuthType::None);
    QCOMPARE(auth_type_from_string("jwt"), AuthType::Jwt);
    QCOMPARE(auth_type_from_string("api_key"), AuthType::ApiKey);
    QCOMPARE(auth_type_from_string("basic"), AuthType::Basic);
    QCOMPARE(auth_type_from_string("oauth2"), AuthType::OAuth2);

    QCOMPARE(qs(to_string(AuthType::Jwt)), QString("jwt"));

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, auth_type_from_string("session"));
}

void TestSecuritySchemeConfig::authentication_jwtAlgorithmFromString_shouldParseValidValues()
{
    QCOMPARE(jwt_algorithm_from_string("HS256"), JwtAlgorithm::HS256);
    QCOMPARE(jwt_algorithm_from_string("HS384"), JwtAlgorithm::HS384);
    QCOMPARE(jwt_algorithm_from_string("HS512"), JwtAlgorithm::HS512);
    QCOMPARE(jwt_algorithm_from_string("RS256"), JwtAlgorithm::RS256);
    QCOMPARE(jwt_algorithm_from_string("ES256"), JwtAlgorithm::ES256);

    QCOMPARE(qs(to_string(JwtAlgorithm::HS256)), QString("HS256"));

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, jwt_algorithm_from_string("none"));
}

void TestSecuritySchemeConfig::authentication_jwtWithValidSecret_shouldPass()
{
    auto cfg = AuthentificationConfig::jwt_with_secret(
        strongSecret(),
        JwtAlgorithm::HS256
        );

    QVERIFY(cfg.is_enabled());
    QVERIFY(cfg.uses_symmetric_key());
    QVERIFY(!cfg.uses_asymmetric_key());

    QVERIFY_THROWS_NO_EXCEPTION(cfg.validate());
}

void TestSecuritySchemeConfig::authentication_jwtWithShortSecret_shouldThrow()
{
    auto cfg = AuthentificationConfig::jwt_with_secret(
        "short-secret",
        JwtAlgorithm::HS256
        );

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cfg.validate());
}

void TestSecuritySchemeConfig::authentication_jwtAsymmetricWithoutPublicKey_shouldThrow()
{
    AuthentificationConfig cfg;
    cfg.set_type(AuthType::Jwt);
    cfg.set_jwt_algorithm(JwtAlgorithm::RS256);

    QVERIFY(cfg.uses_asymmetric_key());
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cfg.validate());
}

void TestSecuritySchemeConfig::authentication_apiKeyWithEmptyHeader_shouldThrow()
{
    auto cfg = AuthentificationConfig::api_key("");
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cfg.validate());
}

void TestSecuritySchemeConfig::authentication_oauth2MissingUrls_shouldThrow()
{
    AuthentificationConfig cfg;
    cfg.set_type(AuthType::OAuth2);

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cfg.validate());

    cfg.set_oauth2_issuer_url("https://issuer.example.com");

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cfg.validate());
}

// ─────────────────────────────────────────────
// TokenTrackingConfig
// ─────────────────────────────────────────────

void TestSecuritySchemeConfig::tokenTracking_disabled_shouldSkipValidation()
{
    auto cfg = TokenTrackingConfig::disabled();

    QVERIFY(!cfg.is_enabled());
    QVERIFY_THROWS_NO_EXCEPTION(cfg.validate());
}

void TestSecuritySchemeConfig::tokenTracking_enabledWithDefaults_shouldBeValid()
{
    TokenTrackingConfig cfg;
    cfg.set_enabled(true);

    QVERIFY(cfg.is_enabled());
    QCOMPARE(QString::fromStdString(cfg.refresh_table()), QString("RefreshToken"));
    QCOMPARE(QString::fromStdString(cfg.revoked_table()), QString("RevokedToken"));

    QVERIFY_THROWS_NO_EXCEPTION(cfg.validate());
}

void TestSecuritySchemeConfig::tokenTracking_sameTables_shouldThrow()
{
    TokenTrackingConfig cfg;
    cfg.set_enabled(true);
    cfg.set_refresh_table("Token");
    cfg.set_revoked_table("Token");

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cfg.validate());
}

void TestSecuritySchemeConfig::tokenTracking_cacheEnabledWithZeroSize_shouldThrow()
{
    TokenTrackingConfig cfg;
    cfg.set_enabled(true);

    TokenTrackingConfig::CacheConfig cache;
    cache.enabled = true;
    cache.max_size = 0;

    cfg.set_cache(cache);

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cfg.validate());
}

void TestSecuritySchemeConfig::tokenTracking_autoCleanupInvalidInterval_shouldThrow()
{
    TokenTrackingConfig cfg;
    cfg.set_enabled(true);

    TokenTrackingConfig::AutoCleanupConfig cleanup;
    cleanup.enabled = true;
    cleanup.interval = std::chrono::seconds(0);

    cfg.set_auto_cleanup(cleanup);

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cfg.validate());
}

// ─────────────────────────────────────────────
// CorsConfig
// ─────────────────────────────────────────────

void TestSecuritySchemeConfig::cors_denyAll_shouldBeDisabled()
{
    auto cfg = CorsConfig::deny_all();

    QVERIFY(!cfg.is_enabled());
    QVERIFY(!cfg.is_wildcard());
    QVERIFY(!cfg.allows_origin("https://example.com"));

    QVERIFY_THROWS_NO_EXCEPTION(cfg.validate());
}

void TestSecuritySchemeConfig::cors_allowAll_shouldUseWildcardWithoutCredentials()
{
    auto cfg = CorsConfig::allow_all();

    QVERIFY(cfg.is_enabled());
    QVERIFY(cfg.is_wildcard());
    QVERIFY(cfg.allows_origin("https://anything.example.com"));
    QVERIFY(!cfg.allow_credentials());

    QVERIFY_THROWS_NO_EXCEPTION(cfg.validate());
}

void TestSecuritySchemeConfig::cors_permissive_shouldAllowConfiguredOrigins()
{
    auto cfg = CorsConfig::permissive({
        "https://app.example.com",
        "https://admin.example.com"
    });

    QVERIFY(cfg.is_enabled());
    QVERIFY(!cfg.is_wildcard());
    QVERIFY(cfg.allow_credentials());
    QVERIFY(cfg.allows_origin("https://app.example.com"));
    QVERIFY(!cfg.allows_origin("https://evil.example.com"));

    QVERIFY_THROWS_NO_EXCEPTION(cfg.validate());
}

void TestSecuritySchemeConfig::cors_credentialsWithWildcard_shouldThrow()
{
    auto cfg = CorsConfig::allow_all();
    cfg.set_allow_credentials(true);

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cfg.validate());
}

void TestSecuritySchemeConfig::cors_enabledWithoutMethods_shouldThrow()
{
    CorsConfig cfg;
    cfg.add_allowed_origin("https://app.example.com");

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cfg.validate());
}

void TestSecuritySchemeConfig::cors_negativeMaxAge_shouldThrow()
{
    auto cfg = CorsConfig::permissive({ "https://app.example.com" });
    cfg.set_max_age(std::chrono::seconds(-1));

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cfg.validate());
}

// ─────────────────────────────────────────────
// HttpLimits
// ─────────────────────────────────────────────

void TestSecuritySchemeConfig::httpLimits_safeDefaults_shouldBeValid()
{
    auto limits = HttpLimits::safe_defaults();

    QVERIFY(limits.max_body_size() > 0);
    QVERIFY(limits.max_header_size() > 0);
    QVERIFY(limits.max_headers_count() > 0);
    QVERIFY(limits.max_url_length() > 0);
    QVERIFY(limits.request_timeout().count() > 0);
    QVERIFY(limits.max_connections_per_ip() > 0);

    QVERIFY_THROWS_NO_EXCEPTION(limits.validate());
}

void TestSecuritySchemeConfig::httpLimits_zeroBodySize_shouldThrow()
{
    auto limits = HttpLimits::safe_defaults();
    limits.set_max_body_size(0);

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, limits.validate());
}

void TestSecuritySchemeConfig::httpLimits_invalidTimeout_shouldThrow()
{
    auto limits = HttpLimits::safe_defaults();
    limits.set_request_timeout(std::chrono::seconds(0));

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, limits.validate());
}

void TestSecuritySchemeConfig::httpLimits_zeroHeadersCount_shouldThrow()
{
    auto limits = HttpLimits::safe_defaults();
    limits.set_max_headers_count(0);

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, limits.validate());
}

// ─────────────────────────────────────────────
// RateLimitRule
// ─────────────────────────────────────────────

void TestSecuritySchemeConfig::rateLimitScope_fromString_shouldParseValidValues()
{
    QCOMPARE(scope_from_string("per_ip"), RateLimitScope::PerIp);
    QCOMPARE(scope_from_string("per_user"), RateLimitScope::PerUser);
    QCOMPARE(scope_from_string("per_api_key"), RateLimitScope::PerApiKey);
    QCOMPARE(scope_from_string("global"), RateLimitScope::Global);

    QCOMPARE(qs(to_string(RateLimitScope::PerIp)), QString("per_ip"));

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, scope_from_string("tenant"));
}

void TestSecuritySchemeConfig::rateLimitRule_validRule_shouldPass()
{
    auto rule = RateLimitRule::per_ip(
        100,
        std::chrono::seconds(60),
        100
        );

    QCOMPARE(rule.scope(), RateLimitScope::PerIp);
    QCOMPARE(rule.requests(), std::uint32_t(100));
    QCOMPARE(rule.window(), std::chrono::seconds(60));
    QCOMPARE(rule.burst(), std::uint32_t(100));

    QVERIFY_THROWS_NO_EXCEPTION(rule.validate());
}

void TestSecuritySchemeConfig::rateLimitRule_zeroRequests_shouldThrow()
{
    auto rule = RateLimitRule::per_ip(
        0,
        std::chrono::seconds(60),
        0
        );

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, rule.validate());
}

void TestSecuritySchemeConfig::rateLimitRule_burstLowerThanRequests_shouldThrow()
{
    auto rule = RateLimitRule::per_ip(
        100,
        std::chrono::seconds(60),
        50
        );

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, rule.validate());
}

void TestSecuritySchemeConfig::rateLimitRule_refillRate_shouldComputeRequestsPerSecond()
{
    auto rule = RateLimitRule::per_user(
        120,
        std::chrono::seconds(60),
        120
        );

    QCOMPARE(rule.refill_rate_per_second(), 2.0);
}

// ─────────────────────────────────────────────
// SecurityHeaders
// ─────────────────────────────────────────────

void TestSecuritySchemeConfig::securityHeaders_none_shouldDisableAllHeaders()
{
    auto headers = SecurityHeaders::none();

    QVERIFY(!headers.hsts().has_value());
    QVERIFY(!headers.content_type_options().has_value());
    QVERIFY(!headers.frame_options().has_value());
    QVERIFY(!headers.referrer_policy().has_value());
    QVERIFY(!headers.content_security_policy().has_value());
    QVERIFY(!headers.permissions_policy().has_value());
}

void TestSecuritySchemeConfig::securityHeaders_recommended_shouldSetCommonHeaders()
{
    auto headers = SecurityHeaders::recommended();

    QVERIFY(headers.hsts().has_value());
    QVERIFY(headers.content_type_options().has_value());
    QVERIFY(headers.frame_options().has_value());
    QVERIFY(headers.referrer_policy().has_value());
    QVERIFY(headers.content_security_policy().has_value());
    QVERIFY(headers.permissions_policy().has_value());

    QCOMPARE(QString::fromStdString(*headers.content_type_options()),
             QString("nosniff"));
}

void TestSecuritySchemeConfig::securityHeaders_strict_shouldSetCrossOriginHeaders()
{
    auto headers = SecurityHeaders::strict();

    QVERIFY(headers.cross_origin_opener_policy().has_value());
    QVERIFY(headers.cross_origin_resource_policy().has_value());

    QCOMPARE(QString::fromStdString(*headers.cross_origin_opener_policy()),
             QString("same-origin"));
}

void TestSecuritySchemeConfig::securityHeaders_disableHeader_shouldClearValue()
{
    auto headers = SecurityHeaders::recommended();

    QVERIFY(headers.hsts().has_value());

    headers.disable_hsts();

    QVERIFY(!headers.hsts().has_value());
}

// ─────────────────────────────────────────────
// SecurityConfig
// ─────────────────────────────────────────────

void TestSecuritySchemeConfig::securityConfig_safeDefaults_shouldBeValid()
{
    auto cfg = SecurityConfig::safe_defaults();

    QVERIFY_THROWS_NO_EXCEPTION(cfg.validate());
}

void TestSecuritySchemeConfig::securityConfig_disabled_shouldBeValid()
{
    auto cfg = SecurityConfig::disabled();

    QVERIFY_THROWS_NO_EXCEPTION(cfg.validate());
}

void TestSecuritySchemeConfig::securityConfig_invalidRateLimit_shouldThrow()
{
    auto cfg = SecurityConfig::safe_defaults();

    RateLimitRule invalid = RateLimitRule::per_ip(
        0,
        std::chrono::seconds(60),
        0
        );

    cfg.add_rate_limit(invalid);

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cfg.validate());
}

void TestSecuritySchemeConfig::securityConfig_corsWildcardWithCredentials_shouldThrow()
{
    auto cfg = SecurityConfig::safe_defaults();

    auto cors = CorsConfig::allow_all();
    cors.set_allow_credentials(true);

    cfg.set_cors(cors);

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cfg.validate());
}
// ─────────────────────────────────────────────
// COUVERTURE ADDITIONNELLE
// ─────────────────────────────────────────────

// ── HttpLimits ────────────────────────────────────────────────

void TestSecuritySchemeConfig::httpLimits_zeroUrlLength_shouldThrow()
{
    auto limits = HttpLimits::safe_defaults();
    limits.set_max_url_length(0);

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, limits.validate());
}

void TestSecuritySchemeConfig::httpLimits_excessiveBodySize_shouldThrow()
{
    // Une taille de corps déraisonnable (> 100 GB) est rejetée.
    auto limits = HttpLimits::safe_defaults();
    limits.set_max_body_size(200ULL * 1024 * 1024 * 1024);   // 200 GB

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, limits.validate());
}

void TestSecuritySchemeConfig::httpLimits_excessiveTimeout_shouldThrow()
{
    // Un request_timeout > 24h est traité comme une erreur de config.
    auto limits = HttpLimits::safe_defaults();
    limits.set_request_timeout(std::chrono::hours(48));

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, limits.validate());
}

void TestSecuritySchemeConfig::httpLimits_zeroConnectionsPerIp_shouldThrow()
{
    auto limits = HttpLimits::safe_defaults();
    limits.set_max_connections_per_ip(0);

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, limits.validate());
}

// ── RateLimitRule : factories non couvertes ───────────────────

void TestSecuritySchemeConfig::rateLimitRule_globalFactory_shouldHaveGlobalScope()
{
    auto rule = RateLimitRule::global(
        1000,
        std::chrono::seconds(60),
        1000
        );

    QCOMPARE(rule.scope(), RateLimitScope::Global);
    QVERIFY_THROWS_NO_EXCEPTION(rule.validate());
}

void TestSecuritySchemeConfig::rateLimitRule_perApiKeyFactory_shouldHaveApiKeyScope()
{
    // per_api_key n'est pas une factory dédiée : on construit la règle
    // et on vérifie que le scope PerApiKey est bien supporté de bout
    // en bout (scope_from_string + to_string).
    QCOMPARE(scope_from_string("per_api_key"), RateLimitScope::PerApiKey);
    QCOMPARE(qs(to_string(RateLimitScope::PerApiKey)), QString("per_api_key"));
    QCOMPARE(qs(to_string(RateLimitScope::Global)), QString("global"));
    QCOMPARE(qs(to_string(RateLimitScope::PerUser)), QString("per_user"));
}

// ── AuthentificationConfig : types non couverts ───────────────

void TestSecuritySchemeConfig::authentication_basicAuth_shouldValidate()
{
    // HTTP Basic Auth n'a aucun champ obligatoire : validate() passe.
    AuthentificationConfig cfg;
    cfg.set_type(AuthType::Basic);

    QVERIFY(cfg.is_enabled());
    QVERIFY_THROWS_NO_EXCEPTION(cfg.validate());
}

void TestSecuritySchemeConfig::authentication_oauth2WithBothUrls_shouldPass()
{
    // OAuth2 valide dès que issuer_url ET jwks_url sont renseignés.
    AuthentificationConfig cfg;
    cfg.set_type(AuthType::OAuth2);
    cfg.set_oauth2_issuer_url("https://issuer.example.com");
    cfg.set_oauth2_jwks_url("https://issuer.example.com/.well-known/jwks.json");

    QVERIFY_THROWS_NO_EXCEPTION(cfg.validate());
}

// ── TokenTrackingConfig : cas positif auto-cleanup ────────────

void TestSecuritySchemeConfig::tokenTracking_autoCleanupValidInterval_shouldPass()
{
    // Un auto-cleanup avec un intervalle strictement positif est valide.
    TokenTrackingConfig cfg;
    cfg.set_enabled(true);

    TokenTrackingConfig::AutoCleanupConfig cleanup;
    cleanup.enabled  = true;
    cleanup.interval = std::chrono::seconds(3600);

    cfg.set_auto_cleanup(cleanup);

    QVERIFY_THROWS_NO_EXCEPTION(cfg.validate());
}

// ── SecurityHeaders : disable des autres en-têtes ─────────────

void TestSecuritySchemeConfig::securityHeaders_disableContentTypeOptions_shouldClearValue()
{
    auto headers = SecurityHeaders::recommended();
    QVERIFY(headers.content_type_options().has_value());

    headers.disable_content_type_options();

    QVERIFY(!headers.content_type_options().has_value());
}

void TestSecuritySchemeConfig::securityHeaders_disableFrameOptions_shouldClearValue()
{
    auto headers = SecurityHeaders::recommended();
    QVERIFY(headers.frame_options().has_value());

    headers.disable_frame_options();

    QVERIFY(!headers.frame_options().has_value());
}

// ── CookieConfig : cas positif ────────────────────────────────

void TestSecuritySchemeConfig::cookieConfig_sameSiteStrictWithoutSecure_shouldPass()
{
    // Seul SameSite=None impose secure=true. Strict reste valide même
    // sans secure.
    auto cfg = CookieConfig::safe_defaults();
    cfg.set_same_site(SameSitePolicy::Strict);
    cfg.set_secure(false);

    QVERIFY_THROWS_NO_EXCEPTION(cfg.validate());
}

// ── CorsConfig : désactivé ────────────────────────────────────

void TestSecuritySchemeConfig::cors_disabledShouldNotValidateMethods()
{
    // Un CorsConfig désactivé (aucune origine) ne valide rien :
    // l'absence de méthodes ne déclenche pas d'erreur.
    CorsConfig cfg;

    QVERIFY(!cfg.is_enabled());
    QVERIFY_THROWS_NO_EXCEPTION(cfg.validate());
}

// ── SecurityConfig : propagation HttpLimits ───────────────────

void TestSecuritySchemeConfig::securityConfig_invalidHttpLimits_shouldThrow()
{
    // Une HttpLimits invalide injectée dans SecurityConfig fait
    // échouer la validation globale.
    auto cfg = SecurityConfig::safe_defaults();

    auto badLimits = HttpLimits::safe_defaults();
    badLimits.set_max_body_size(0);

    cfg.set_http_limits(badLimits);

    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cfg.validate());
}