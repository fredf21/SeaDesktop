#include "test_jwt_service.h"

#include "security/jwt_service.h"

#include <jwt-cpp/jwt.h>

#include <chrono>
#include <optional>
#include <string>

using sea::infrastructure::security::JwtService;
using sea::infrastructure::security::JwtClaims;
using sea::infrastructure::security::TokenType;
using sea::infrastructure::security::GenerateTokenParams;
using sea::infrastructure::security::VerifyTokenParams;

namespace {

// Secret de test suffisamment long (HS256).
const std::string kSecret = "test_secret_key_that_is_long_enough_xx";
const std::string kIssuer = "sea-desktop-test";

// Construit des params d'access token valides par défaut.
GenerateTokenParams makeAccessParams() {
    GenerateTokenParams p{};
    p.user_id    = "user-123";
    p.email      = "user@example.com";
    p.role       = "admin";
    p.secret     = kSecret;
    p.issuer     = kIssuer;
    p.token_type = TokenType::Access;
    p.ttl        = std::chrono::seconds(900);   // 15 min
    return p;
}

// Construit des params de refresh token valides par défaut.
GenerateTokenParams makeRefreshParams() {
    GenerateTokenParams p{};
    p.user_id    = "user-123";
    p.secret     = kSecret;
    p.issuer     = kIssuer;
    p.token_type = TokenType::Refresh;
    p.ttl        = std::chrono::seconds(604800); // 7 jours
    return p;
}

// Construit des params de vérification cohérents avec un access token.
VerifyTokenParams makeAccessVerifyParams(const std::string& token) {
    VerifyTokenParams v{};
    v.token           = token;
    v.secret          = kSecret;
    v.expected_issuer = kIssuer;
    v.expected_type   = TokenType::Access;
    return v;
}

} // namespace

// ═════════════════════════════════════════════════════════════
// 1. Helpers de conversion
// ═════════════════════════════════════════════════════════════

void TestJwtService::tokenTypeToString_shouldMatch() {
    QCOMPARE(QString::fromStdString(JwtService::token_type_to_string(TokenType::Access)),
             QString("access"));
    QCOMPARE(QString::fromStdString(JwtService::token_type_to_string(TokenType::Refresh)),
             QString("refresh"));
}

void TestJwtService::tokenTypeFromString_validValues_shouldMatch() {
    const auto access = JwtService::token_type_from_string("access");
    const auto refresh = JwtService::token_type_from_string("refresh");

    QVERIFY(access.has_value());
    QVERIFY(refresh.has_value());
    QCOMPARE(*access, TokenType::Access);
    QCOMPARE(*refresh, TokenType::Refresh);
}

void TestJwtService::tokenTypeFromString_invalidValue_shouldReturnNullopt() {
    QVERIFY(!JwtService::token_type_from_string("bearer").has_value());
    QVERIFY(!JwtService::token_type_from_string("").has_value());
    QVERIFY(!JwtService::token_type_from_string("ACCESS").has_value());
}

// ═════════════════════════════════════════════════════════════
// 2. generate_token : succès et validations d'entrée
// ═════════════════════════════════════════════════════════════

void TestJwtService::generateAccessToken_shouldProduceNonEmptyToken() {
    const std::string token = JwtService::generate_token(makeAccessParams());
    QVERIFY(!token.empty());
}

void TestJwtService::generateRefreshToken_shouldProduceNonEmptyToken() {
    const std::string token = JwtService::generate_token(makeRefreshParams());
    QVERIFY(!token.empty());
}

void TestJwtService::generateToken_emptySecret_shouldThrow() {
    GenerateTokenParams p = makeAccessParams();
    p.secret.clear();
    verifyThrows<std::invalid_argument>([&]() {
        [[maybe_unused]] auto token = JwtService::generate_token(p);
    });
}

void TestJwtService::generateToken_emptyUserId_shouldThrow() {
    GenerateTokenParams p = makeAccessParams();
    p.user_id.clear();
    verifyThrows<std::invalid_argument>([&]() {
        [[maybe_unused]] auto token = JwtService::generate_token(p);
    });
}

void TestJwtService::generateToken_emptyIssuer_shouldThrow() {
    GenerateTokenParams p = makeAccessParams();
    p.issuer.clear();
    verifyThrows<std::invalid_argument>([&]() {
        [[maybe_unused]] auto token = JwtService::generate_token(p);
    });
}

void TestJwtService::generateToken_zeroTtl_shouldThrow() {
    GenerateTokenParams p = makeAccessParams();
    p.ttl = std::chrono::seconds(0);
    verifyThrows<std::invalid_argument>([&]() {
        [[maybe_unused]] auto token = JwtService::generate_token(p);
    });
}

void TestJwtService::generateToken_negativeTtl_shouldThrow() {
    GenerateTokenParams p = makeAccessParams();
    p.ttl = std::chrono::seconds(-60);
    verifyThrows<std::invalid_argument>([&]() {
        [[maybe_unused]] auto token = JwtService::generate_token(p);
    });
}

void TestJwtService::generateToken_hasThreeSegments() {
    // Un JWT est composé de 3 segments base64url séparés par des points :
    // header.payload.signature
    const std::string token = JwtService::generate_token(makeAccessParams());

    const auto first = token.find('.');
    QVERIFY(first != std::string::npos);
    const auto second = token.find('.', first + 1);
    QVERIFY(second != std::string::npos);
    // Aucun troisième point.
    QCOMPARE(token.find('.', second + 1), std::string::npos);
}

void TestJwtService::generateToken_explicitJti_shouldBeUsed() {
    // Quand un jti explicite est fourni, il doit se retrouver dans
    // les claims du token vérifié.
    GenerateTokenParams p = makeAccessParams();
    p.jti = "fixed-jti-for-test-0001";

    const std::string token = JwtService::generate_token(p);
    const auto claims = JwtService::verify_token(makeAccessVerifyParams(token));

    QVERIFY(claims.has_value());
    QCOMPARE(QString::fromStdString(claims->jti),
             QString("fixed-jti-for-test-0001"));
}

void TestJwtService::generateToken_autoJti_shouldBeGenerated() {
    // Sans jti explicite, le service génère un UUID v4 non vide.
    GenerateTokenParams p = makeAccessParams();
    QVERIFY(p.jti.empty());

    const std::string token = JwtService::generate_token(p);
    const auto claims = JwtService::verify_token(makeAccessVerifyParams(token));

    QVERIFY(claims.has_value());
    QVERIFY(!claims->jti.empty());
    // Un UUID v4 fait 36 caractères (8-4-4-4-12 avec tirets).
    QCOMPARE(claims->jti.size(), std::size_t(36));
}

// ═════════════════════════════════════════════════════════════
// 3. verify_token : round-trips réussis
// ═════════════════════════════════════════════════════════════

void TestJwtService::verifyValidAccessToken_shouldReturnClaims() {
    const std::string token = JwtService::generate_token(makeAccessParams());
    const auto claims = JwtService::verify_token(makeAccessVerifyParams(token));

    QVERIFY(claims.has_value());
    QCOMPARE(claims->token_type, TokenType::Access);
}

void TestJwtService::verifyValidRefreshToken_shouldReturnClaims() {
    const std::string token = JwtService::generate_token(makeRefreshParams());

    VerifyTokenParams v{};
    v.token           = token;
    v.secret          = kSecret;
    v.expected_issuer = kIssuer;
    v.expected_type   = TokenType::Refresh;

    const auto claims = JwtService::verify_token(v);
    QVERIFY(claims.has_value());
    QCOMPARE(claims->token_type, TokenType::Refresh);
}

void TestJwtService::verifyToken_preservesUserId() {
    GenerateTokenParams p = makeAccessParams();
    p.user_id = "specific-user-id-42";

    const std::string token = JwtService::generate_token(p);
    const auto claims = JwtService::verify_token(makeAccessVerifyParams(token));

    QVERIFY(claims.has_value());
    QCOMPARE(QString::fromStdString(claims->user_id),
             QString("specific-user-id-42"));
}

void TestJwtService::verifyToken_preservesIssuer() {
    const std::string token = JwtService::generate_token(makeAccessParams());
    const auto claims = JwtService::verify_token(makeAccessVerifyParams(token));

    QVERIFY(claims.has_value());
    QCOMPARE(QString::fromStdString(claims->issuer), QString::fromStdString(kIssuer));
}

void TestJwtService::verifyToken_preservesEmailAndRole() {
    GenerateTokenParams p = makeAccessParams();
    p.email = "alice@sea.test";
    p.role  = "manager";

    const std::string token = JwtService::generate_token(p);
    const auto claims = JwtService::verify_token(makeAccessVerifyParams(token));

    QVERIFY(claims.has_value());
    QCOMPARE(QString::fromStdString(claims->email), QString("alice@sea.test"));
    QCOMPARE(QString::fromStdString(claims->role), QString("manager"));
}

void TestJwtService::verifyToken_iatAndExpAreConsistent() {
    // exp doit être strictement postérieur à iat, et l'écart doit
    // correspondre au ttl demandé.
    GenerateTokenParams p = makeAccessParams();
    p.ttl = std::chrono::seconds(900);

    const std::string token = JwtService::generate_token(p);
    const auto claims = JwtService::verify_token(makeAccessVerifyParams(token));

    QVERIFY(claims.has_value());
    QVERIFY(claims->expires_at > claims->issued_at);
    QCOMPARE(claims->expires_at - claims->issued_at, std::int64_t(900));
}

// ═════════════════════════════════════════════════════════════
// 4. verify_token : rejets (nullopt) — verify_token ne lève jamais
// ═════════════════════════════════════════════════════════════

void TestJwtService::verifyEmptyToken_shouldReturnNullopt() {
    VerifyTokenParams v = makeAccessVerifyParams("");
    QVERIFY(!JwtService::verify_token(v).has_value());
}

void TestJwtService::verifyToken_emptySecret_shouldReturnNullopt() {
    const std::string token = JwtService::generate_token(makeAccessParams());

    VerifyTokenParams v = makeAccessVerifyParams(token);
    v.secret.clear();
    QVERIFY(!JwtService::verify_token(v).has_value());
}

void TestJwtService::verifyToken_wrongSecret_shouldReturnNullopt() {
    const std::string token = JwtService::generate_token(makeAccessParams());

    VerifyTokenParams v = makeAccessVerifyParams(token);
    v.secret = "a_completely_different_wrong_secret_key";
    QVERIFY(!JwtService::verify_token(v).has_value());
}

void TestJwtService::verifyToken_wrongIssuer_shouldReturnNullopt() {
    const std::string token = JwtService::generate_token(makeAccessParams());

    VerifyTokenParams v = makeAccessVerifyParams(token);
    v.expected_issuer = "some-other-issuer";
    QVERIFY(!JwtService::verify_token(v).has_value());
}

void TestJwtService::verifyToken_wrongType_shouldReturnNullopt() {
    // Un refresh token vérifié en tant qu'access token est rejeté.
    const std::string token = JwtService::generate_token(makeRefreshParams());

    VerifyTokenParams v{};
    v.token           = token;
    v.secret          = kSecret;
    v.expected_issuer = kIssuer;
    v.expected_type   = TokenType::Access;   // mauvais type attendu

    QVERIFY(!JwtService::verify_token(v).has_value());
}

void TestJwtService::verifyMalformedToken_shouldReturnNullopt() {
    VerifyTokenParams v = makeAccessVerifyParams("this.is.not-a-valid-jwt");
    QVERIFY(!JwtService::verify_token(v).has_value());
}

void TestJwtService::verifyExpiredToken_shouldReturnNullopt() {
    // generate_token refuse un ttl <= 0, donc on forge directement
    // un token déjà expiré avec jwt-cpp pour tester le rejet.
    const auto now = std::chrono::system_clock::now();
    const std::string expiredToken =
        jwt::create()
            .set_type("JWT")
            .set_issuer(kIssuer)
            .set_subject("user-123")
            .set_id("expired-jti")
            .set_issued_at(now - std::chrono::hours(2))
            .set_expires_at(now - std::chrono::hours(1))   // expiré il y a 1h
            .set_payload_claim("token_type", jwt::claim(std::string("access")))
            .sign(jwt::algorithm::hs256{kSecret});

    VerifyTokenParams v = makeAccessVerifyParams(expiredToken);
    QVERIFY(!JwtService::verify_token(v).has_value());
}

void TestJwtService::verifyToken_neverThrows() {
    // Quelle que soit l'entrée, verify_token ne doit jamais lever.
    const char* badTokens[] = {
        "", "x", "a.b", "a.b.c.d", "...", "not base64 at all",
        "eyJhbGciOiJIUzI1NiJ9.garbage.sig"
    };

    for (const char* bad : badTokens) {
        verifyNoThrow([&]() {
            VerifyTokenParams v = makeAccessVerifyParams(bad);
            [[maybe_unused]] auto r = JwtService::verify_token(v);
        });
    }
}

// ═════════════════════════════════════════════════════════════
// 5. Claims
// ═════════════════════════════════════════════════════════════

void TestJwtService::additionalClaims_shouldRoundTrip() {
    // Les claims custom d'un access token doivent survivre au round-trip.
    GenerateTokenParams p = makeAccessParams();
    p.additional_claims["tenant_id"]  = "tenant-007";
    p.additional_claims["department"] = "engineering";

    const std::string token = JwtService::generate_token(p);
    const auto claims = JwtService::verify_token(makeAccessVerifyParams(token));

    QVERIFY(claims.has_value());
    QVERIFY(claims->additional_claims.count("tenant_id") == 1);
    QVERIFY(claims->additional_claims.count("department") == 1);
    QCOMPARE(QString::fromStdString(claims->additional_claims.at("tenant_id")),
             QString("tenant-007"));
    QCOMPARE(QString::fromStdString(claims->additional_claims.at("department")),
             QString("engineering"));
}

void TestJwtService::refreshToken_doesNotCarryEmailRole() {
    // Un refresh token ne porte ni email ni role (claims access-only).
    GenerateTokenParams p = makeRefreshParams();
    p.email = "should@be.ignored";   // ignoré pour un refresh token
    p.role  = "should_be_ignored";

    const std::string token = JwtService::generate_token(p);

    VerifyTokenParams v{};
    v.token           = token;
    v.secret          = kSecret;
    v.expected_issuer = kIssuer;
    v.expected_type   = TokenType::Refresh;

    const auto claims = JwtService::verify_token(v);
    QVERIFY(claims.has_value());
    QVERIFY(claims->email.empty());
    QVERIFY(claims->role.empty());
}

void TestJwtService::jti_shouldRoundTrip() {
    GenerateTokenParams p = makeRefreshParams();
    p.jti = "refresh-jti-unique-9999";

    const std::string token = JwtService::generate_token(p);

    VerifyTokenParams v{};
    v.token           = token;
    v.secret          = kSecret;
    v.expected_issuer = kIssuer;
    v.expected_type   = TokenType::Refresh;

    const auto claims = JwtService::verify_token(v);
    QVERIFY(claims.has_value());
    QCOMPARE(QString::fromStdString(claims->jti),
             QString("refresh-jti-unique-9999"));
}