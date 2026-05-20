#pragma once

#include <QtTest/QtTest>
#include <QTemporaryFile>
#include <QTextStream>
#include <QString>

#include <exception>
#include <stdexcept>
#include <type_traits>

class TestYamlSchemaParser : public QObject {
    Q_OBJECT

private:
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
    void parseMinimalProject_shouldSucceed();
    void missingServices_shouldThrow();
    void servicesMustBeSequence_shouldThrow();
    void invalidServicePort_shouldThrow();
    void parseEntityWithFields_shouldSucceed();
    void unknownFieldType_shouldThrow();
    void parseRelation_shouldSucceed();
    void manyToManyWithoutPivotTable_shouldThrow();
    void parseDatabaseMemoryByDefault_shouldSucceed();
    void invalidDatabaseType_shouldThrow();
    void parsePaginationPage_shouldSucceed();
    void emptyPagination_shouldThrow();
    void fileFieldWithoutFileBlock_shouldThrow();
    void fileBlockOnNonFileField_shouldThrow();
    void parseStorageConfig_shouldSucceed();
    void invalidStorageBackend_shouldThrow();
    void parseLoggingConfig_shouldSucceed();
    void invalidLoggingLevel_shouldThrow();
};