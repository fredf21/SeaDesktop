#pragma once

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QString>

#include <exception>
#include <stdexcept>
#include <type_traits>

// ─────────────────────────────────────────────────────────────
// TestSecretStore
//
// Suite de tests des fonctions libres de secret_store :
// génération, persistance fichier, résolution du secret JWT.
//
// Ces tests font de VRAIES I/O disque (lecture/écriture du
// fichier "<service>.jwt.secret") : chaque test reçoit un
// répertoire de stockage isolé via QTemporaryDir (init/cleanup).
//
// Attention aux variables d'environnement : resolve_jwt_secret
// consulte SEA_JWT_SECRET_<SERVICE> en priorité. Les tests
// utilisent des noms de service spécifiques et nettoient l'env.
//
// Organisation des slots :
//   1. build_service_jwt_env_name (normalisation, vide)
//   2. generate_secure_random_secret (longueur, charset, unicité)
//   3. save / load secret fichier (round-trip, absence, vide)
//   4. resolve_jwt_secret (env > fichier > génération)
// ─────────────────────────────────────────────────────────────
class TestSecretStore : public QObject {
    Q_OBJECT

private:
    QTemporaryDir* tempDir_ = nullptr;

    std::string storageDir() const {
        return tempDir_->path().toStdString();
    }

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
    // ── Fixtures ──────────────────────────────────────────────
    void init();
    void cleanup();

    // ── 1. build_service_jwt_env_name ─────────────────────────
    void envName_simpleService_shouldBePrefixedAndUppercased();
    void envName_serviceWithSpecialChars_shouldNormalizeToUnderscore();
    void envName_emptyService_shouldThrow();

    // ── 2. generate_secure_random_secret ──────────────────────
    void generateSecret_defaultLength_shouldBe64();
    void generateSecret_customLength_shouldMatch();
    void generateSecret_zeroLength_shouldThrow();
    void generateSecret_onlyAllowedCharset();
    void generateSecret_twoCalls_shouldDiffer();

    // ── 3. save / load secret fichier ─────────────────────────
    void saveThenLoad_shouldRoundTrip();
    void loadNonExistentSecret_shouldReturnNullopt();
    void loadFromEmptyStorageDir_shouldReturnNullopt();
    void saveEmptySecret_shouldThrow();
    void saveCreatesStorageDirectory_shouldSucceed();
    void loadTrimsTrailingWhitespace_shouldSucceed();
    void saveOverwritesExistingSecret_shouldSucceed();

    // ── 4. resolve_jwt_secret ─────────────────────────────────
    void resolveSecret_emptyServiceName_shouldThrow();
    void resolveSecret_emptyStorageDir_shouldThrow();
    void resolveSecret_fromEnvVariable_shouldUseEnv();
    void resolveSecret_fromFile_whenNoEnv();
    void resolveSecret_generatesAndPersists_whenNothingExists();
    void resolveSecret_isStableAcrossCalls();
};