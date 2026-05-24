#pragma once

#include <QtTest/QtTest>
#include <QTemporaryFile>
#include <QTextStream>
#include <QString>

#include <exception>
#include <stdexcept>
#include <type_traits>

// ─────────────────────────────────────────────────────────────
// TestYamlSchemaParser
//
// Suite de tests exhaustive du YamlSchemaParser.
//
// Organisation des slots par section du parser :
//   1.  Document racine / project
//   2.  Service (port, sous-blocs)
//   3.  Entity (options, table_name, sous-listes)
//   4.  Field (types, contraintes, default, native)
//   5.  File field (sous-bloc file:)
//   6.  Storage (backend, modes octaux)
//   7.  Relations (kinds, on_delete, many_to_many)
//   8.  Database (types, migrations)
//   9.  Seeds (mode, on_error, alias, M2M)
//   10. Pagination (page / offset / cursor)
//   11. Security (authentication, cors, rate_limits, http_limits, headers)
//   12. Authorization / ABAC
//   13. Logging (level, modules, sinks, rotation, async)
//   14. Helpers (parse_duration, parse_size, resolve_env)
// ─────────────────────────────────────────────────────────────
class TestYamlSchemaParser : public QObject {
    Q_OBJECT

private:
    // Écrit `content` dans un fichier YAML temporaire et renvoie son chemin.
    QString writeTempYaml(const QString& content) {
        QTemporaryFile file;
        file.setAutoRemove(false);

        if (!file.open()) {
            throw std::runtime_error("Impossible de créer un fichier YAML temporaire");
        }

        QTextStream out(&file);
        out << content;
        file.close();

        return file.fileName();
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
    // ── 1. Document racine / project ──────────────────────────
    void parseMinimalProject_shouldSucceed();
    void rootMustBeMap_shouldThrow();
    void rootEmptyDocument_shouldThrow();
    void projectMustBeObject_shouldThrow();
    void projectWithoutName_shouldThrow();
    void missingProjectBlock_usesUnnamedProject();
    void missingServices_shouldThrow();
    void servicesMustBeSequence_shouldThrow();
    void multipleServices_shouldSucceed();

    // ── 2. Service ────────────────────────────────────────────
    void serviceWithoutName_shouldThrow();
    void serviceNotAnObject_shouldThrow();
    void invalidServicePortTooHigh_shouldThrow();
    void invalidServicePortZero_shouldThrow();
    void servicePortDefault_shouldBe8080();
    void servicePortBoundaryLow_shouldSucceed();
    void servicePortBoundaryHigh_shouldSucceed();
    void databaseNotAnObject_shouldThrow();
    void storageNotAnObject_shouldThrow();
    void securityNotAnObject_shouldThrow();

    // ── 3. Entity ─────────────────────────────────────────────
    void parseEntityWithFields_shouldSucceed();
    void entityWithoutName_shouldThrow();
    void entityNotAnObject_shouldThrow();
    void entityCustomTableName_shouldOverrideDefault();
    void entityDerivedTableName_shouldBePlural();
    void entityOptions_shouldBeParsed();
    void entityOptionsNotAnObject_shouldThrow();
    void fieldsNotASequence_shouldThrow();
    void relationsNotASequence_shouldThrow();
    void seedsNotASequence_shouldThrow();
    void paginationNotAMap_shouldThrow();

    // ── 4. Field ──────────────────────────────────────────────
    void fieldWithoutName_shouldThrow();
    void fieldWithoutType_shouldThrow();
    void fieldNotAnObject_shouldThrow();
    void unknownFieldType_shouldThrow();
    void allSimpleFieldTypes_shouldSucceed();
    void fieldRequiredUniqueIndexed_shouldBeParsed();
    void passwordFieldNotSerializableByDefault_shouldBeParsed();
    void passwordFieldExplicitSerializable_shouldBeParsed();
    void fieldMaxLength_shouldBeParsed();
    void fieldMinMaxValueInteger_shouldBeParsed();
    void fieldMinMaxValueFloat_shouldBeParsed();
    void fieldDefaultString_shouldBeParsed();
    void fieldDefaultInteger_shouldBeParsed();
    void fieldDefaultBool_shouldBeParsed();
    void fieldDefaultOnBinary_shouldThrow();
    void fieldDefaultOnFile_shouldThrow();
    void fieldPreviousName_shouldBeParsed();
    void fieldUnsignedValue_shouldBeParsed();
    void nativeNodeOnNonNativeType_shouldThrow();

    // ── 5. File field ─────────────────────────────────────────
    void fileFieldWithoutFileBlock_shouldThrow();
    void fileBlockOnNonFileField_shouldThrow();
    void fileBlockNotAnObject_shouldThrow();
    void fileFieldComplete_shouldSucceed();
    void fileFieldMaxSizeZero_shouldThrow();
    void fileFieldInvalidMimeType_shouldThrow();
    void fileFieldEmptyExtension_shouldThrow();
    void fileFieldExtensionWithSeparator_shouldThrow();
    void fileFieldExtensionNormalization_shouldSucceed();
    void fileFieldInvalidOnDelete_shouldThrow();
    void fileFieldOnDeleteValues_shouldSucceed();

    // ── 6. Storage ────────────────────────────────────────────
    void parseStorageConfig_shouldSucceed();
    void invalidStorageBackend_shouldThrow();
    void storageBackendCaseInsensitive_shouldSucceed();
    void storageInvalidFileMode_shouldThrow();
    void storageMinimalBlock_shouldSucceed();

    // ── 7. Relations ──────────────────────────────────────────
    void parseRelation_shouldSucceed();
    void relationWithoutName_shouldThrow();
    void relationWithoutTargetEntity_shouldThrow();
    void relationWithoutKind_shouldThrow();
    void relationUnknownKind_shouldThrow();
    void relationAllKinds_shouldSucceed();
    void relationInvalidOnDelete_shouldThrow();
    void manyToManyWithoutPivotTable_shouldThrow();
    void manyToManyWithoutSourceFk_shouldThrow();
    void manyToManyWithoutTargetFk_shouldThrow();
    void manyToManyComplete_shouldSucceed();
    void relationNotAnObject_shouldThrow();

    // ── 8. Database ───────────────────────────────────────────
    void parseDatabaseMemoryByDefault_shouldSucceed();
    void invalidDatabaseType_shouldThrow();
    void databaseAllTypes_shouldSucceed();
    void databaseFullConfig_shouldBeParsed();
    void migrationsNotAnObject_shouldThrow();
    void migrationsValidModes_shouldSucceed();
    void migrationsInvalidMode_shouldThrow();

    // ── 9. Seeds ──────────────────────────────────────────────
    void seedsBasicRecord_shouldSucceed();
    void seedsInvalidMode_shouldThrow();
    void seedsInvalidOnError_shouldThrow();
    void seedRecordNotAnObject_shouldThrow();
    void seedRecordWithAlias_shouldBeParsed();
    void seedRecordM2MNotASequence_shouldThrow();
    void seedRecordM2MWithAliases_shouldBeParsed();

    // ── 10. Pagination ────────────────────────────────────────
    void parsePaginationPage_shouldSucceed();
    void emptyPagination_shouldThrow();
    void paginationOffset_shouldSucceed();
    void paginationCursor_shouldSucceed();
    void paginationCursorWithoutCursorField_shouldThrow();
    void paginationCursorWithoutSort_shouldThrow();
    void paginationAllModes_shouldSucceed();
    void paginationPageNotAMap_shouldThrow();

    // ── 11. Security ──────────────────────────────────────────
    void securityNoBlock_disabledByDefault();
    void securityAuthNone_shouldSucceed();
    void securityAuthJwtComplete_shouldSucceed();
    void securityAuthJwtShortSecret_shouldThrow();
    void securityAuthInvalidType_shouldThrow();
    void securityAuthInvalidJwtAlgorithm_shouldThrow();
    void securityRateLimitComplete_shouldSucceed();
    void securityRateLimitWithoutRequests_shouldThrow();
    void securityRateLimitWithoutWindow_shouldThrow();
    void securityRateLimitInvalidScope_shouldThrow();
    void securityRateLimitsNotASequence_shouldThrow();
    void securityHttpLimits_shouldSucceed();
    void securityHeadersPresets_shouldSucceed();
    void securityHeadersInvalidPreset_shouldThrow();
    void securityCookieConfig_shouldSucceed();

    // ── 12. Authorization / ABAC ──────────────────────────────
    void authorizationDisabled_shouldSucceed();
    void authorizationEnabledWithRoles_shouldSucceed();
    void authorizationInvalidDefaultPolicy_shouldThrow();
    void authorizationInvalidAbacMode_shouldThrow();
    void entityAccessControlAllowRoles_shouldSucceed();
    void entityAccessControlUnknownOperation_shouldThrow();
    void entityAccessControlUndeclaredRole_shouldThrow();
    void entityAccessControlSameScopeWithoutScopeField_shouldThrow();
    void entityAccessControlOwnResourceWithoutOwnerField_shouldThrow();

    // ── 13. Logging ───────────────────────────────────────────
    void parseLoggingConfig_shouldSucceed();
    void invalidLoggingLevel_shouldThrow();
    void loggingModulesNotAMap_shouldThrow();
    void loggingSinksNotASequence_shouldThrow();
    void loggingSinkWithoutType_shouldThrow();
    void loggingInvalidSinkType_shouldThrow();
    void loggingInvalidFlushLevel_shouldThrow();
    void loggingInvalidOverflowPolicy_shouldThrow();
    void loggingMinimalBlock_shouldSucceed();

    // ── 14. Helpers (via YAML) ────────────────────────────────
    void durationSuffixes_shouldBeParsed();
    void invalidDurationSuffix_shouldThrow();
    void sizeSuffixes_shouldBeParsed();
    void invalidSizeSuffix_shouldThrow();
    void envVariableResolution_shouldSucceed();
    void envVariableMissing_shouldThrow();
};