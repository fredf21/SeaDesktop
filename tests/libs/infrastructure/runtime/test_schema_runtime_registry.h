#pragma once

#include <QtTest/QtTest>
#include <QString>

#include <exception>
#include <type_traits>

// ─────────────────────────────────────────────────────────────
// TestSchemaRuntimeRegistry
//
// Suite de tests du SchemaRuntimeRegistry : un index en mémoire
// des entités d'un Schema, utilisé par le CRUD générique pour
// retrouver une entité (et ses champs) à partir de son nom.
//
// Le registre est un objet à état simple, sans I/O ni Seastar :
// chaque test construit son propre Schema et son propre registre.
//
// Organisation des slots :
//   1. register_schema / find_entity
//   2. has_entity
//   3. find_field
//   4. clear et ré-enregistrement
// ─────────────────────────────────────────────────────────────
class TestSchemaRuntimeRegistry : public QObject {
    Q_OBJECT

private:
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
    // ── 1. register_schema / find_entity ──────────────────────
    void emptyRegistry_findEntity_shouldReturnNull();
    void registerSchema_findKnownEntity_shouldReturnPointer();
    void findEntity_unknownName_shouldReturnNull();
    void findEntity_isCaseSensitive();
    void registerEmptySchema_shouldSucceed();
    void registerMultipleEntities_allFindable();

    // ── 2. has_entity ─────────────────────────────────────────
    void hasEntity_knownEntity_shouldReturnTrue();
    void hasEntity_unknownEntity_shouldReturnFalse();
    void hasEntity_onEmptyRegistry_shouldReturnFalse();

    // ── 3. find_field ─────────────────────────────────────────
    void findField_knownEntityKnownField_shouldReturnPointer();
    void findField_knownEntityUnknownField_shouldReturnNull();
    void findField_unknownEntity_shouldReturnNull();
    void findField_returnsCorrectFieldData();

    // ── 4. clear et ré-enregistrement ─────────────────────────
    void clear_emptiesRegistry();
    void registerSchema_replacesPreviousContent();
    void clearThenRegister_shouldWork();
};