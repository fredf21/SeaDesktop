#include "test_secret_store.h"

#include "security/secret_store.h"
#include "exception_handling.h"

#include <QByteArray>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace ss = sea::infrastructure::security;
using SecurityError = sea::sea_errors_handling::SECURITY_ERROR;

namespace {

// Jeu de caractères autorisé par generate_secure_random_secret.
const std::string kAllowedCharset =
    "0123456789"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "-_";

bool isAllowedSecret(const std::string& secret) {
    for (char c : secret) {
        if (kAllowedCharset.find(c) == std::string::npos) {
            return false;
        }
    }
    return true;
}

} // namespace

// ═════════════════════════════════════════════════════════════
// Fixtures
// ═════════════════════════════════════════════════════════════

void TestSecretStore::init() {
    tempDir_ = new QTemporaryDir();
    QVERIFY2(tempDir_->isValid(),
             "Impossible de créer le répertoire temporaire de test");
}

void TestSecretStore::cleanup() {
    delete tempDir_;
    tempDir_ = nullptr;
}

// ═════════════════════════════════════════════════════════════
// 1. build_service_jwt_env_name
// ═════════════════════════════════════════════════════════════

void TestSecretStore::envName_simpleService_shouldBePrefixedAndUppercased() {
    const std::string name = ss::build_service_jwt_env_name("ApiService");
    QCOMPARE(QString::fromStdString(name),
             QString("SEA_JWT_SECRET_APISERVICE"));
}

void TestSecretStore::envName_serviceWithSpecialChars_shouldNormalizeToUnderscore() {
    // Tout caractère non alphanumérique devient '_'.
    const std::string name = ss::build_service_jwt_env_name("My-Cool.Service 2");
    QCOMPARE(QString::fromStdString(name),
             QString("SEA_JWT_SECRET_MY_COOL_SERVICE_2"));
}

void TestSecretStore::envName_emptyService_shouldThrow() {
    verifyThrows<SecurityError>([&]() {
        [[maybe_unused]] auto n = ss::build_service_jwt_env_name("");
    });
}

// ═════════════════════════════════════════════════════════════
// 2. generate_secure_random_secret
// ═════════════════════════════════════════════════════════════

void TestSecretStore::generateSecret_defaultLength_shouldBe64() {
    const std::string secret = ss::generate_secure_random_secret();
    QCOMPARE(secret.size(), std::size_t(64));
}

void TestSecretStore::generateSecret_customLength_shouldMatch() {
    for (std::size_t len : {1u, 16u, 32u, 128u, 256u}) {
        const std::string secret = ss::generate_secure_random_secret(len);
        QCOMPARE(secret.size(), len);
    }
}

void TestSecretStore::generateSecret_zeroLength_shouldThrow() {
    verifyThrows<SecurityError>([&]() {
        [[maybe_unused]] auto s = ss::generate_secure_random_secret(0);
    });
}

void TestSecretStore::generateSecret_onlyAllowedCharset() {
    // Le secret ne doit contenir que des caractères du charset défini
    // (alphanumériques + '-' + '_'), donc URL-safe.
    const std::string secret = ss::generate_secure_random_secret(512);
    QVERIFY(isAllowedSecret(secret));
}

void TestSecretStore::generateSecret_twoCalls_shouldDiffer() {
    // Deux générations consécutives ne doivent pas produire le même
    // secret (probabilité de collision négligeable sur 64 chars).
    const std::string a = ss::generate_secure_random_secret();
    const std::string b = ss::generate_secure_random_secret();
    QVERIFY(a != b);
}

// ═════════════════════════════════════════════════════════════
// 3. save / load secret fichier
// ═════════════════════════════════════════════════════════════

void TestSecretStore::saveThenLoad_shouldRoundTrip() {
    const std::string service = "RoundTripService";
    const std::string secret  = "my-persisted-secret-value-123";

    ss::save_secret_to_file(storageDir(), service, secret);
    const auto loaded = ss::load_secret_from_file(storageDir(), service);

    QVERIFY(loaded.has_value());
    QCOMPARE(QString::fromStdString(*loaded), QString::fromStdString(secret));
}

void TestSecretStore::loadNonExistentSecret_shouldReturnNullopt() {
    // Aucun fichier secret pour ce service : load renvoie nullopt.
    const auto loaded = ss::load_secret_from_file(storageDir(), "NeverSavedService");
    QVERIFY(!loaded.has_value());
}

void TestSecretStore::loadFromEmptyStorageDir_shouldReturnNullopt() {
    // storageDir vide : make_secret_file_path échoue silencieusement
    // (renvoie ""), donc load renvoie nullopt sans lever.
    std::optional<std::string> loaded;
    verifyNoThrow([&]() {
        loaded = ss::load_secret_from_file("", "SomeService");
    });
    QVERIFY(!loaded.has_value());
}

void TestSecretStore::saveEmptySecret_shouldThrow() {
    verifyThrows<SecurityError>([&]() {
        ss::save_secret_to_file(storageDir(), "SomeService", "");
    });
}

void TestSecretStore::saveCreatesStorageDirectory_shouldSucceed() {
    // save_secret_to_file crée la hiérarchie de dossiers manquante.
    const std::string nestedDir = storageDir() + "/nested/secrets/dir";
    QVERIFY(!std::filesystem::exists(nestedDir));

    verifyNoThrow([&]() {
        ss::save_secret_to_file(nestedDir, "NestedService", "secret-in-nested");
    });

    QVERIFY(std::filesystem::is_directory(nestedDir));
    const auto loaded = ss::load_secret_from_file(nestedDir, "NestedService");
    QVERIFY(loaded.has_value());
    QCOMPARE(QString::fromStdString(*loaded), QString("secret-in-nested"));
}

void TestSecretStore::loadTrimsTrailingWhitespace_shouldSucceed() {
    // load_secret_from_file applique un trim_right : un fichier dont
    // le contenu se termine par des retours ligne est nettoyé.
    const std::string service  = "TrimService";
    const std::string filePath = storageDir() + "/" + service + ".jwt.secret";

    {
        std::ofstream ofs(filePath);
        ofs << "secret-with-trailing\n\n  ";
    }

    const auto loaded = ss::load_secret_from_file(storageDir(), service);
    QVERIFY(loaded.has_value());
    QCOMPARE(QString::fromStdString(*loaded), QString("secret-with-trailing"));
}

void TestSecretStore::saveOverwritesExistingSecret_shouldSucceed() {
    const std::string service = "OverwriteService";

    ss::save_secret_to_file(storageDir(), service, "first-secret");
    ss::save_secret_to_file(storageDir(), service, "second-secret-replacing");

    const auto loaded = ss::load_secret_from_file(storageDir(), service);
    QVERIFY(loaded.has_value());
    QCOMPARE(QString::fromStdString(*loaded), QString("second-secret-replacing"));
}

// ═════════════════════════════════════════════════════════════
// 4. resolve_jwt_secret
// ═════════════════════════════════════════════════════════════

void TestSecretStore::resolveSecret_emptyServiceName_shouldThrow() {
    ss::JwtSecretConfig cfg{};
    cfg.storageDir  = storageDir();
    cfg.serviceName = "";

    verifyThrows<SecurityError>([&]() {
        [[maybe_unused]] auto s = ss::resolve_jwt_secret(cfg);
    });
}

void TestSecretStore::resolveSecret_emptyStorageDir_shouldThrow() {
    ss::JwtSecretConfig cfg{};
    cfg.storageDir  = "";
    cfg.serviceName = "SomeService";

    verifyThrows<SecurityError>([&]() {
        [[maybe_unused]] auto s = ss::resolve_jwt_secret(cfg);
    });
}

void TestSecretStore::resolveSecret_fromEnvVariable_shouldUseEnv() {
    // Si la variable d'env SEA_JWT_SECRET_<SERVICE> est définie,
    // resolve_jwt_secret l'utilise en priorité (ni fichier ni génération).
    const std::string service = "EnvPriorityService";
    const std::string envName = ss::build_service_jwt_env_name(service);
    const std::string envValue = "secret-coming-from-environment";

    qputenv(envName.c_str(), QByteArray(envValue.c_str()));

    ss::JwtSecretConfig cfg{};
    cfg.storageDir  = storageDir();
    cfg.serviceName = service;

    const std::string resolved = ss::resolve_jwt_secret(cfg);
    QCOMPARE(QString::fromStdString(resolved), QString::fromStdString(envValue));

    // Aucun fichier ne doit avoir été écrit (l'env a court-circuité).
    const auto onDisk = ss::load_secret_from_file(storageDir(), service);
    QVERIFY(!onDisk.has_value());

    qunsetenv(envName.c_str());
}

void TestSecretStore::resolveSecret_fromFile_whenNoEnv() {
    // Pas de variable d'env, mais un fichier secret existe :
    // resolve_jwt_secret renvoie le secret du fichier.
    const std::string service = "FileSourceService";
    const std::string envName = ss::build_service_jwt_env_name(service);
    qunsetenv(envName.c_str());

    const std::string fileSecret = "secret-already-on-disk-xyz";
    ss::save_secret_to_file(storageDir(), service, fileSecret);

    ss::JwtSecretConfig cfg{};
    cfg.storageDir  = storageDir();
    cfg.serviceName = service;

    const std::string resolved = ss::resolve_jwt_secret(cfg);
    QCOMPARE(QString::fromStdString(resolved), QString::fromStdString(fileSecret));
}

void TestSecretStore::resolveSecret_generatesAndPersists_whenNothingExists() {
    // Ni env ni fichier : resolve_jwt_secret génère un secret ET
    // le persiste sur disque pour les appels suivants.
    const std::string service = "FreshGenerationService";
    const std::string envName = ss::build_service_jwt_env_name(service);
    qunsetenv(envName.c_str());

    QVERIFY(!ss::load_secret_from_file(storageDir(), service).has_value());

    ss::JwtSecretConfig cfg{};
    cfg.storageDir  = storageDir();
    cfg.serviceName = service;

    const std::string resolved = ss::resolve_jwt_secret(cfg);
    QVERIFY(!resolved.empty());

    // Le secret généré doit avoir été persisté.
    const auto persisted = ss::load_secret_from_file(storageDir(), service);
    QVERIFY(persisted.has_value());
    QCOMPARE(QString::fromStdString(*persisted), QString::fromStdString(resolved));
}

void TestSecretStore::resolveSecret_isStableAcrossCalls() {
    // Deux résolutions successives (sans env) doivent renvoyer le
    // MÊME secret : le premier appel génère+persiste, le second relit.
    const std::string service = "StableService";
    const std::string envName = ss::build_service_jwt_env_name(service);
    qunsetenv(envName.c_str());

    ss::JwtSecretConfig cfg{};
    cfg.storageDir  = storageDir();
    cfg.serviceName = service;

    const std::string first  = ss::resolve_jwt_secret(cfg);
    const std::string second = ss::resolve_jwt_secret(cfg);

    QVERIFY(!first.empty());
    QCOMPARE(QString::fromStdString(first), QString::fromStdString(second));
}

