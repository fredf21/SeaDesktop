#pragma once

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QString>

#include <exception>
#include <type_traits>

// ─────────────────────────────────────────────────────────────
// TestFilesystemStorage
//
// Suite de tests du backend FilesystemStorage (implémentation
// concrète de IFileStorage basée sur std::filesystem).
//
// Ces tests font de VRAIES I/O disque : chaque test reçoit une
// racine de storage isolée via un QTemporaryDir créé dans init()
// et détruit dans cleanup(). Aucun test ne touche le filesystem
// hors de cette racine temporaire.
//
// Organisation des slots :
//   1. Construction (backend, root_path, racine existante)
//   2. store        (écriture, sous-dossiers, écrasement, binaire)
//   3. retrieve     (lecture, round-trip, introuvable)
//   4. remove       (suppression, idempotence)
//   5. exists       (présence / absence)
//   6. size         (taille, introuvable)
//   7. Sandboxing   (path vide, absolu, traversal, faux-préfixe)
//   8. Round-trips combinés
// ─────────────────────────────────────────────────────────────
class TestFilesystemStorage : public QObject {
    Q_OBJECT

private:
    // Racine temporaire isolée, recréée pour chaque test.
    QTemporaryDir* tempDir_ = nullptr;

    // Renvoie le chemin de la racine temporaire courante.
    std::string rootPath() const {
        return tempDir_->path().toStdString();
    }

    // Vérifie qu'un appel ne lève aucune exception.
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

    // Vérifie qu'un appel lève bien une exception du type attendu.
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

private slots:
    // ── Fixtures Qt (appelées automatiquement) ────────────────
    void init();      // avant chaque test : crée la racine temporaire
    void cleanup();   // après chaque test : détruit la racine temporaire

    // ── 1. Construction ───────────────────────────────────────
    void constructWithValidConfig_shouldSucceed();
    void constructWithEmptyRootPath_shouldThrow();
    void constructCreatesRootDirectory_shouldSucceed();
    void constructWithExistingRoot_shouldSucceed();
    void constructWithFileAsRoot_shouldThrow();
    void rootIsCanonicalized_shouldSucceed();

    // ── 2. store ──────────────────────────────────────────────
    void storeSimpleFile_shouldCreateFile();
    void storeCreatesSubdirectories_shouldSucceed();
    void storeOverwritesExistingFile_shouldSucceed();
    void storeBinaryContent_shouldPreserveBytes();
    void storeEmptyContent_shouldSucceed();
    void storeEmptyRelativePath_shouldThrow();

    // ── 3. retrieve ───────────────────────────────────────────
    void retrieveStoredFile_shouldReturnContent();
    void retrieveRoundTripBinary_shouldMatch();
    void retrieveNonExistentFile_shouldThrow();
    void retrieveEmptyPath_shouldThrow();

    // ── 4. remove ─────────────────────────────────────────────
    void removeExistingFile_shouldReturnTrue();
    void removeNonExistentFile_shouldReturnFalse();
    void removeIsIdempotent_shouldNotThrow();

    // ── 5. exists ─────────────────────────────────────────────
    void existsStoredFile_shouldReturnTrue();
    void existsNonExistentFile_shouldReturnFalse();
    void existsAfterRemove_shouldReturnFalse();

    // ── 6. size ───────────────────────────────────────────────
    void sizeStoredFile_shouldReturnByteCount();
    void sizeEmptyFile_shouldReturnZero();
    void sizeNonExistentFile_shouldThrow();

    // ── 7. Sandboxing (resolve_safe_path) ─────────────────────
    void storeAbsolutePath_shouldThrow();
    void storeParentTraversal_shouldThrow();
    void storeDeepTraversal_shouldThrow();
    void retrieveParentTraversal_shouldThrow();
    void storePathStaysInsideSandbox_shouldSucceed();
    void siblingPrefixPath_shouldThrow();
    void normalizedInnerTraversal_shouldSucceed();

    // ── 8. Round-trips combinés ───────────────────────────────
    void fullLifecycle_storeRetrieveRemove_shouldSucceed();
    void storeRetrieveMultipleFiles_shouldBeIndependent();
};