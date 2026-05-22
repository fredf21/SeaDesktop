#pragma once

#include <QObject>

#include <seastar/core/future.hh>

#include <stdexcept>
#include <utility>

// ─────────────────────────────────────────────────────────────
// TestInMemoryGenericRepository
//
// Suite de tests du InMemoryGenericRepository.
//
// NOTE SUR SEASTAR
// ────────────────
// Toutes les méthodes du repository renvoient un seastar::future.
// InMemoryGenericRepository est cependant un backend purement
// mémoire : chaque méthode produit son résultat via
// seastar::make_ready_future — le future est donc TOUJOURS déjà
// résolu, sans suspension ni I/O réelle.
//
// On ne peut pas appeler future::get() depuis un thread Qt normal
// (get() exige un seastar::thread / reactor actif). Le helper
// await_ready() ci-dessous extrait la valeur d'un future qui est
// garanti prêt, sans reactor : il vérifie available() puis récupère
// la valeur. Si un jour une méthode devenait réellement asynchrone,
// available() serait faux et le helper lèverait — signal clair qu'il
// faut alors un vrai harness Seastar.
// ─────────────────────────────────────────────────────────────
class TestInMemoryGenericRepository : public QObject
{
    Q_OBJECT

protected:
    // Extrait la valeur d'un future DÉJÀ résolu (backend mémoire).
    // Lève std::logic_error si le future n'est pas prêt — ce qui
    // signalerait que la méthode testée est devenue asynchrone.
    template <typename T>
    static T await_ready(seastar::future<T>&& fut)
    {
        if (!fut.available()) {
            throw std::logic_error(
                "await_ready: le future n'est pas resolu — "
                "le backend memoire est cense etre synchrone");
        }
        return std::move(fut).get();
    }

    // Surcharge pour seastar::future<void>.
    static void await_ready(seastar::future<>&& fut)
    {
        if (!fut.available()) {
            throw std::logic_error(
                "await_ready: le future<void> n'est pas resolu");
        }
        std::move(fut).get();
    }

private slots:
    void create_withValidId_shouldStoreRecord();
    void create_withoutId_shouldReturnNullopt();
    void create_duplicateId_shouldReturnNullopt();
    void create_shouldIgnoreManyToManyVectorString();

    void findById_existingRecord_shouldReturnRecord();
    void findById_unknownRecord_shouldReturnNullopt();

    void findAll_existingEntity_shouldReturnAllRecords();
    void findOneByField_matchingString_shouldReturnRecord();

    void update_existingRecord_shouldModifyFields();
    void update_unknownRecord_shouldFail();
    void update_shouldNotModifyId();
    void update_shouldIgnoreManyToManyVectorString();

    void remove_existingRecord_shouldReturnTrue();
    void remove_unknownRecord_shouldReturnFalse();

    void count_existingEntity_shouldReturnNumberOfRecords();

    void listPage_shouldReturnRequestedPage();
    void listOffset_shouldReturnRequestedSlice();
    void listCursor_shouldReturnItemsAfterCursor();

    void incrementField_int32_shouldIncrementValue();
    void incrementField_missingField_shouldReturnFalse();
    void incrementField_nonNumericField_shouldReturnFalse();

    // ── Couverture additionnelle ──────────────────────────────

    // count / find_all sur entité inexistante
    void count_unknownEntity_shouldReturnZero();
    void count_emptyAfterRemovingAll_shouldReturnZero();
    void findAll_unknownEntity_shouldReturnEmpty();

    // create avec id entier
    void create_withInt64Id_shouldStoreRecord();

    // find_one_by_field : cas non couverts
    void findOneByField_noMatch_shouldReturnNullopt();
    void findOneByField_unknownEntity_shouldReturnNullopt();
    void findOneByField_numericField_shouldMatchViaStringConversion();

    // increment_field : cas non couverts
    void incrementField_unknownEntity_shouldReturnFalse();
    void incrementField_unknownRecord_shouldReturnFalse();
    void incrementField_negativeDelta_shouldDecrement();

    // in_transaction
    void inTransaction_workReturnsTrue_shouldCommit();
    void inTransaction_workReturnsFalse_shouldNotCommit();
    void inTransaction_workThrows_shouldNotCommit();
    void inTransaction_canMutateRepository();

    // insert_pivot
    void insertPivot_shouldStoreLink();
    void insertPivot_sameKeyTwice_shouldOverwrite();

    // pagination : cas limites
    void listPage_pageZero_shouldBehaveAsFirstPage();
    void listPage_pageSizeZero_shouldReturnEmpty();
    void listPage_offsetBeyondTotal_shouldReturnEmptyButKeepTotal();
    void listPage_sortDescending_shouldReverseOrder();
    void listOffset_offsetBeyondTotal_shouldReturnEmpty();
    void listCursor_noCursor_shouldReturnFromStart();
    void listCursor_lastPage_shouldHaveNoNextCursor();
    void listPage_emptyEntity_shouldReturnEmpty();

    // isolation entre entités
    void storage_distinctEntities_shouldNotInterfere();
};