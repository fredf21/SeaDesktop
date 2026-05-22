#include "test_filesystem_storage.h"

#include "exception_handling.h"
#include "storage/filesystem_storage.h"
#include "storage_config.h"

#include <filesystem>
#include <fstream>
#include <string>

using sea::infrastructure::storage::FilesystemStorage;
using sea::domain::StorageConfig;
using sea::domain::StorageBackend;
using StorageError = sea::sea_errors_handling::StorageException;

namespace {

// Construit une StorageConfig Filesystem valide pointant sur `root`.
StorageConfig makeConfig(const std::string& root) {
    StorageConfig cfg{};
    cfg.backend   = StorageBackend::Filesystem;
    cfg.root_path = root;
    return cfg;
}

} // namespace

// ═════════════════════════════════════════════════════════════
// Fixtures : une racine temporaire isolée par test
// ═════════════════════════════════════════════════════════════

void TestFilesystemStorage::init() {
    tempDir_ = new QTemporaryDir();
    QVERIFY2(tempDir_->isValid(),
             "Impossible de créer le répertoire temporaire de test");
}

void TestFilesystemStorage::cleanup() {
    delete tempDir_;   // détruit le dossier temporaire et son contenu
    tempDir_ = nullptr;
}

// ═════════════════════════════════════════════════════════════
// 1. Construction
// ═════════════════════════════════════════════════════════════

void TestFilesystemStorage::constructWithValidConfig_shouldSucceed() {
    verifyNoThrow([&]() {
        FilesystemStorage storage(makeConfig(rootPath()));
    });
}

void TestFilesystemStorage::constructWithEmptyRootPath_shouldThrow() {
    StorageConfig cfg = makeConfig("");
    verifyThrows<StorageError>([&]() {
        FilesystemStorage storage(cfg);
    });
}

void TestFilesystemStorage::constructCreatesRootDirectory_shouldSucceed() {
    // La racine n'existe pas encore : le constructeur doit la créer.
    const std::string newRoot = rootPath() + "/nested/storage/root";
    QVERIFY(!std::filesystem::exists(newRoot));

    verifyNoThrow([&]() {
        FilesystemStorage storage(makeConfig(newRoot));
    });

    QVERIFY(std::filesystem::is_directory(newRoot));
}

void TestFilesystemStorage::constructWithExistingRoot_shouldSucceed() {
    // La racine temporaire existe déjà : la construction est idempotente.
    verifyNoThrow([&]() {
        FilesystemStorage storage(makeConfig(rootPath()));
    });
}

void TestFilesystemStorage::constructWithFileAsRoot_shouldThrow() {
    // root_path pointe vers un fichier existant, pas un dossier.
    const std::string filePath = rootPath() + "/iam_a_file";
    {
        std::ofstream ofs(filePath);
        ofs << "content";
    }
    QVERIFY(std::filesystem::is_regular_file(filePath));

    verifyThrows<StorageError>([&]() {
        FilesystemStorage storage(makeConfig(filePath));
    });
}

void TestFilesystemStorage::rootIsCanonicalized_shouldSucceed() {
    // root() doit renvoyer un chemin absolu canonicalisé.
    FilesystemStorage storage(makeConfig(rootPath()));

    const auto& root = storage.root();
    QVERIFY(root.is_absolute());
    QVERIFY(std::filesystem::exists(root));
    // Le chemin canonicalisé ne doit plus contenir de composant '..'.
    for (const auto& part : root) {
        QVERIFY(part != "..");
    }
}

// ═════════════════════════════════════════════════════════════
// 2. store
// ═════════════════════════════════════════════════════════════

void TestFilesystemStorage::storeSimpleFile_shouldCreateFile() {
    FilesystemStorage storage(makeConfig(rootPath()));

    storage.store("hello.txt", "Hello SeaDesktop");

    const std::string onDisk = rootPath() + "/hello.txt";
    QVERIFY(std::filesystem::is_regular_file(onDisk));
}

void TestFilesystemStorage::storeCreatesSubdirectories_shouldSucceed() {
    // store doit créer toute la hiérarchie de sous-dossiers (mkdir -p).
    FilesystemStorage storage(makeConfig(rootPath()));

    storage.store("users/avatars/2024/avatar.png", "fake-png-bytes");

    const std::string onDisk = rootPath() + "/users/avatars/2024/avatar.png";
    QVERIFY(std::filesystem::is_regular_file(onDisk));
    QVERIFY(std::filesystem::is_directory(rootPath() + "/users/avatars/2024"));
}

void TestFilesystemStorage::storeOverwritesExistingFile_shouldSucceed() {
    // store écrase silencieusement un fichier existant au même path.
    FilesystemStorage storage(makeConfig(rootPath()));

    storage.store("doc.txt", "premiere version");
    storage.store("doc.txt", "deuxieme version remplacante");

    const std::string content = storage.retrieve("doc.txt");
    QCOMPARE(QString::fromStdString(content),
             QString("deuxieme version remplacante"));
}

void TestFilesystemStorage::storeBinaryContent_shouldPreserveBytes() {
    // Le contenu binaire (octets nuls inclus) doit être préservé.
    FilesystemStorage storage(makeConfig(rootPath()));

    std::string binary;
    binary.push_back('\x00');
    binary.push_back('\xFF');
    binary.push_back('\x00');
    binary.push_back('\x42');
    binary.push_back('\x7F');
    binary.push_back('\x80');

    storage.store("blob.bin", binary);

    const std::string readBack = storage.retrieve("blob.bin");
    QCOMPARE(readBack.size(), binary.size());
    QVERIFY(readBack == binary);
}

void TestFilesystemStorage::storeEmptyContent_shouldSucceed() {
    FilesystemStorage storage(makeConfig(rootPath()));

    verifyNoThrow([&]() {
        storage.store("empty.dat", std::string{});
    });

    QVERIFY(storage.exists("empty.dat"));
    QCOMPARE(storage.size("empty.dat"), std::size_t(0));
}

void TestFilesystemStorage::storeEmptyRelativePath_shouldThrow() {
    FilesystemStorage storage(makeConfig(rootPath()));

    verifyThrows<StorageError>([&]() {
        storage.store("", "content");
    });
}

// ═════════════════════════════════════════════════════════════
// 3. retrieve
// ═════════════════════════════════════════════════════════════

void TestFilesystemStorage::retrieveStoredFile_shouldReturnContent() {
    FilesystemStorage storage(makeConfig(rootPath()));

    storage.store("note.txt", "contenu de la note");
    const std::string content = storage.retrieve("note.txt");

    QCOMPARE(QString::fromStdString(content), QString("contenu de la note"));
}

void TestFilesystemStorage::retrieveRoundTripBinary_shouldMatch() {
    // Round-trip complet : store puis retrieve sur 256 octets distincts.
    FilesystemStorage storage(makeConfig(rootPath()));

    std::string payload;
    payload.reserve(256);
    for (int i = 0; i < 256; ++i) {
        payload.push_back(static_cast<char>(i));
    }

    storage.store("media/full_range.bin", payload);
    const std::string readBack = storage.retrieve("media/full_range.bin");

    QCOMPARE(readBack.size(), std::size_t(256));
    QVERIFY(readBack == payload);
}

void TestFilesystemStorage::retrieveNonExistentFile_shouldThrow() {
    FilesystemStorage storage(makeConfig(rootPath()));

    verifyThrows<StorageError>([&]() {
        [[maybe_unused]] auto c = storage.retrieve("does/not/exist.txt");
    });
}

void TestFilesystemStorage::retrieveEmptyPath_shouldThrow() {
    FilesystemStorage storage(makeConfig(rootPath()));

    verifyThrows<StorageError>([&]() {
        [[maybe_unused]] auto c = storage.retrieve("");
    });
}

// ═════════════════════════════════════════════════════════════
// 4. remove
// ═════════════════════════════════════════════════════════════

void TestFilesystemStorage::removeExistingFile_shouldReturnTrue() {
    FilesystemStorage storage(makeConfig(rootPath()));

    storage.store("to_delete.txt", "data");
    QVERIFY(storage.exists("to_delete.txt"));

    const bool removed = storage.remove("to_delete.txt");
    QCOMPARE(removed, true);
    QVERIFY(!storage.exists("to_delete.txt"));
}

void TestFilesystemStorage::removeNonExistentFile_shouldReturnFalse() {
    // remove est idempotent : un fichier absent renvoie false sans throw.
    FilesystemStorage storage(makeConfig(rootPath()));

    bool removed = true;
    verifyNoThrow([&]() {
        removed = storage.remove("never_existed.txt");
    });
    QCOMPARE(removed, false);
}

void TestFilesystemStorage::removeIsIdempotent_shouldNotThrow() {
    // Deux remove successifs sur le même fichier : aucun throw.
    FilesystemStorage storage(makeConfig(rootPath()));

    storage.store("once.txt", "data");

    verifyNoThrow([&]() {
        const bool first  = storage.remove("once.txt");
        const bool second = storage.remove("once.txt");
        QCOMPARE(first, true);
        QCOMPARE(second, false);
    });
}

// ═════════════════════════════════════════════════════════════
// 5. exists
// ═════════════════════════════════════════════════════════════

void TestFilesystemStorage::existsStoredFile_shouldReturnTrue() {
    FilesystemStorage storage(makeConfig(rootPath()));

    storage.store("present.txt", "here");
    QCOMPARE(storage.exists("present.txt"), true);
}

void TestFilesystemStorage::existsNonExistentFile_shouldReturnFalse() {
    FilesystemStorage storage(makeConfig(rootPath()));

    QCOMPARE(storage.exists("absent.txt"), false);
}

void TestFilesystemStorage::existsAfterRemove_shouldReturnFalse() {
    FilesystemStorage storage(makeConfig(rootPath()));

    storage.store("transient.txt", "data");
    QVERIFY(storage.exists("transient.txt"));

    storage.remove("transient.txt");
    QCOMPARE(storage.exists("transient.txt"), false);
}

// ═════════════════════════════════════════════════════════════
// 6. size
// ═════════════════════════════════════════════════════════════

void TestFilesystemStorage::sizeStoredFile_shouldReturnByteCount() {
    FilesystemStorage storage(makeConfig(rootPath()));

    const std::string content = "contenu de taille connue";
    storage.store("sized.txt", content);

    QCOMPARE(storage.size("sized.txt"), content.size());
}

void TestFilesystemStorage::sizeEmptyFile_shouldReturnZero() {
    FilesystemStorage storage(makeConfig(rootPath()));

    storage.store("zero.dat", std::string{});
    QCOMPARE(storage.size("zero.dat"), std::size_t(0));
}

void TestFilesystemStorage::sizeNonExistentFile_shouldThrow() {
    FilesystemStorage storage(makeConfig(rootPath()));

    verifyThrows<StorageError>([&]() {
        [[maybe_unused]] auto s = storage.size("ghost.dat");
    });
}

// ═════════════════════════════════════════════════════════════
// 7. Sandboxing (resolve_safe_path)
// ═════════════════════════════════════════════════════════════

void TestFilesystemStorage::storeAbsolutePath_shouldThrow() {
    // Un path absolu est rejeté d'emblée (premier rempart).
    FilesystemStorage storage(makeConfig(rootPath()));

    verifyThrows<StorageError>([&]() {
        storage.store("/etc/passwd", "malicious");
    });
}

void TestFilesystemStorage::storeParentTraversal_shouldThrow() {
    // Un "../" simple qui sort de la racine est rejeté.
    FilesystemStorage storage(makeConfig(rootPath()));

    verifyThrows<StorageError>([&]() {
        storage.store("../escaped.txt", "malicious");
    });
}

void TestFilesystemStorage::storeDeepTraversal_shouldThrow() {
    // Path traversal profond : doit être bloqué après normalisation.
    FilesystemStorage storage(makeConfig(rootPath()));

    verifyThrows<StorageError>([&]() {
        storage.store("users/../../../../tmp/evil.txt", "malicious");
    });
}

void TestFilesystemStorage::retrieveParentTraversal_shouldThrow() {
    FilesystemStorage storage(makeConfig(rootPath()));

    verifyThrows<StorageError>([&]() {
        [[maybe_unused]] auto c = storage.retrieve("../../etc/hosts");
    });
}

void TestFilesystemStorage::storePathStaysInsideSandbox_shouldSucceed() {
    // Un path multi-niveaux légitime, entièrement dans le sandbox.
    FilesystemStorage storage(makeConfig(rootPath()));

    verifyNoThrow([&]() {
        storage.store("a/b/c/d/e/deep.txt", "legitimate");
    });
    QVERIFY(storage.exists("a/b/c/d/e/deep.txt"));
}

void TestFilesystemStorage::siblingPrefixPath_shouldThrow() {
    // Un "../" menant à un dossier frère partageant un préfixe avec
    // la racine doit être rejeté (le résultat sort du sandbox).
    FilesystemStorage storage(makeConfig(rootPath()));

    verifyThrows<StorageError>([&]() {
        storage.store("../sibling/file.txt", "outside");
    });
}

void TestFilesystemStorage::normalizedInnerTraversal_shouldSucceed() {
    // Un "../" qui reste à l'INTÉRIEUR du sandbox après normalisation
    // est légitime : a/b/../c/file == a/c/file.
    FilesystemStorage storage(makeConfig(rootPath()));

    verifyNoThrow([&]() {
        storage.store("a/b/../c/file.txt", "inner ok");
    });
    QVERIFY(storage.exists("a/c/file.txt"));
}

// ═════════════════════════════════════════════════════════════
// 8. Round-trips combinés
// ═════════════════════════════════════════════════════════════

void TestFilesystemStorage::fullLifecycle_storeRetrieveRemove_shouldSucceed() {
    // Cycle de vie complet d'un fichier : store -> exists -> size
    // -> retrieve -> remove -> exists.
    FilesystemStorage storage(makeConfig(rootPath()));

    const std::string path    = "lifecycle/document.txt";
    const std::string content = "Cycle de vie complet du fichier.";

    storage.store(path, content);

    QCOMPARE(storage.exists(path), true);
    QCOMPARE(storage.size(path), content.size());
    QCOMPARE(QString::fromStdString(storage.retrieve(path)),
             QString::fromStdString(content));

    QCOMPARE(storage.remove(path), true);
    QCOMPARE(storage.exists(path), false);
}

void TestFilesystemStorage::storeRetrieveMultipleFiles_shouldBeIndependent() {
    // Plusieurs fichiers stockés ne doivent pas interférer entre eux.
    FilesystemStorage storage(makeConfig(rootPath()));

    storage.store("dir1/fileA.txt", "contenu A");
    storage.store("dir2/fileB.txt", "contenu B different");
    storage.store("dir1/fileC.txt", "contenu C encore autre");

    QCOMPARE(QString::fromStdString(storage.retrieve("dir1/fileA.txt")),
             QString("contenu A"));
    QCOMPARE(QString::fromStdString(storage.retrieve("dir2/fileB.txt")),
             QString("contenu B different"));
    QCOMPARE(QString::fromStdString(storage.retrieve("dir1/fileC.txt")),
             QString("contenu C encore autre"));

    // Supprimer l'un ne doit pas affecter les autres.
    storage.remove("dir1/fileA.txt");
    QVERIFY(!storage.exists("dir1/fileA.txt"));
    QVERIFY(storage.exists("dir2/fileB.txt"));
    QVERIFY(storage.exists("dir1/fileC.txt"));
}
