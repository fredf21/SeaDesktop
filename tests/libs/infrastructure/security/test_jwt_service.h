#pragma once

#include <QtTest/QtTest>
#include <QString>

#include <exception>
#include <stdexcept>
#include <type_traits>

// ─────────────────────────────────────────────────────────────
// TestJwtService
//
// Suite de tests du JwtService (génération et vérification de
// JSON Web Tokens HS256, basé sur jwt-cpp).
//
// Le service est entièrement statique et sans état : aucun
// fixture n'est nécessaire. Les tests construisent des
// GenerateTokenParams / VerifyTokenParams localement.
//
// Contrats testés :
//   - generate_token LÈVE std::invalid_argument sur entrée invalide
//   - verify_token NE LÈVE JAMAIS : renvoie std::nullopt en cas
//     d'échec (token vide, mauvais secret, mauvais issuer, mauvais
//     type, token expiré, token malformé)
//
// Organisation des slots :
//   1. Helpers de conversion (token_type <-> string)
//   2. generate_token : succès et validations d'entrée
//   3. verify_token : round-trips réussis
//   4. verify_token : rejets (nullopt)
//   5. Claims (additional_claims, jti, access vs refresh)
// ─────────────────────────────────────────────────────────────
class TestJwtService : public QObject {
    Q_OBJECT

private:
    template <typename ExceptionType, typename Func>
    void verifyThrows(Func&& func) {
        static_assert(std::is_base_of_v<std::exception, ExceptionType>,
                      "ExceptionType doit hériter de std::exception");
        try {
            func();
            QFAIL("Exception attendue, mais aucune exception n'a été lancée");
        } catch (const ExceptionType&) {
            QVERIFY(true);
        } catch (const std::exception& e) {
            QFAIL(qPrintable(QString("Mauvais type d'exception lancé: %1").arg(e.what())));
        } catch (...) {
            QFAIL("Mauvais type d'exception lancé: exception inconnue");
        }
    }

    template <typename Func>
    void verifyNoThrow(Func&& func) {
        try {
            func();
            QVERIFY(true);
        } catch (const std::exception& e) {
            QFAIL(qPrintable(QString("Exception inattendue: %1").arg(e.what())));
        } catch (...) {
            QFAIL("Exception inconnue inattendue");
        }
    }

private slots:
    // ── 1. Helpers de conversion ──────────────────────────────
    void tokenTypeToString_shouldMatch();
    void tokenTypeFromString_validValues_shouldMatch();
    void tokenTypeFromString_invalidValue_shouldReturnNullopt();

    // ── 2. generate_token : succès et validations ─────────────
    void generateAccessToken_shouldProduceNonEmptyToken();
    void generateRefreshToken_shouldProduceNonEmptyToken();
    void generateToken_emptySecret_shouldThrow();
    void generateToken_emptyUserId_shouldThrow();
    void generateToken_emptyIssuer_shouldThrow();
    void generateToken_zeroTtl_shouldThrow();
    void generateToken_negativeTtl_shouldThrow();
    void generateToken_hasThreeSegments();
    void generateToken_explicitJti_shouldBeUsed();
    void generateToken_autoJti_shouldBeGenerated();

    // ── 3. verify_token : round-trips réussis ─────────────────
    void verifyValidAccessToken_shouldReturnClaims();
    void verifyValidRefreshToken_shouldReturnClaims();
    void verifyToken_preservesUserId();
    void verifyToken_preservesIssuer();
    void verifyToken_preservesEmailAndRole();
    void verifyToken_iatAndExpAreConsistent();

    // ── 4. verify_token : rejets (nullopt) ────────────────────
    void verifyEmptyToken_shouldReturnNullopt();
    void verifyToken_emptySecret_shouldReturnNullopt();
    void verifyToken_wrongSecret_shouldReturnNullopt();
    void verifyToken_wrongIssuer_shouldReturnNullopt();
    void verifyToken_wrongType_shouldReturnNullopt();
    void verifyMalformedToken_shouldReturnNullopt();
    void verifyExpiredToken_shouldReturnNullopt();
    void verifyToken_neverThrows();

    // ── 5. Claims ─────────────────────────────────────────────
    void additionalClaims_shouldRoundTrip();
    void refreshToken_doesNotCarryEmailRole();
    void jti_shouldRoundTrip();
};