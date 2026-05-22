#pragma once

#include <QtTest/QtTest>
#include <QString>

#include <exception>
#include <type_traits>

// ─────────────────────────────────────────────────────────────
// TestGenericValidator
//
// Suite de tests du GenericValidator : vérifie qu'un
// DynamicRecord respecte la définition d'une Entity.
//
// Le validateur ne lève jamais : il RETOURNE un vector<string>
// d'erreurs (vide == record valide). Les tests inspectent donc
// la taille / le contenu du vecteur retourné.
//
// Deux modes :
//   - validate()         : création — les champs requis absents
//                          sont des erreurs
//   - validate_partial() : update partiel — les champs absents
//                          sont tolérés
//
// Particularités du code testé :
//   - le champ nommé "id" est TOUJOURS ignoré (géré par le backend)
//   - matches_type attend std::int64_t pour Int/BigInt/SmallInt
//   - min/max ne sont comparés que si les types concordent
//
// Organisation des slots :
//   1. validate : record valide
//   2. validate : champs requis manquants / null
//   3. validate : erreurs de type
//   4. validate : email
//   5. validate : max_length
//   6. validate : min_value / max_value
//   7. validate_partial : tolérance des champs absents
//   8. Le champ "id" est ignoré
// ─────────────────────────────────────────────────────────────
class TestGenericValidator : public QObject {
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
    // ── 1. validate : record valide ───────────────────────────
    void validate_validRecord_shouldReturnNoErrors();
    void validate_emptyEntity_shouldReturnNoErrors();
    void validate_allFieldTypes_validRecord_shouldPass();

    // ── 2. validate : champs requis manquants / null ──────────
    void validate_missingRequiredField_shouldReportError();
    void validate_missingOptionalField_shouldPass();
    void validate_missingRequiredFieldWithDefault_shouldPass();
    void validate_nullRequiredField_shouldReportError();
    void validate_nullOptionalField_shouldPass();

    // ── 3. validate : erreurs de type ─────────────────────────
    void validate_wrongTypeStringField_shouldReportError();
    void validate_wrongTypeIntField_shouldReportError();
    void validate_wrongTypeBoolField_shouldReportError();

    // ── 4. validate : email ───────────────────────────────────
    void validate_validEmail_shouldPass();
    void validate_invalidEmail_shouldReportError();
    void validate_emailWithoutAt_shouldReportError();

    // ── 5. validate : max_length ──────────────────────────────
    void validate_stringWithinMaxLength_shouldPass();
    void validate_stringExceedingMaxLength_shouldReportError();
    void validate_stringExactlyMaxLength_shouldPass();

    // ── 6. validate : min_value / max_value ───────────────────
    void validate_intWithinRange_shouldPass();
    void validate_intBelowMin_shouldReportError();
    void validate_intAboveMax_shouldReportError();
    void validate_floatWithinRange_shouldPass();
    void validate_floatBelowMin_shouldReportError();

    // ── 7. validate_partial : tolérance des champs absents ────
    void validatePartial_missingRequiredField_shouldPass();
    void validatePartial_presentInvalidField_shouldReportError();
    void validatePartial_nullRequiredField_shouldReportError();
    void validatePartial_emptyRecord_shouldReturnNoErrors();

    // ── 8. Le champ "id" est ignoré ───────────────────────────
    void validate_idFieldIsAlwaysIgnored();
    void validatePartial_idFieldIsAlwaysIgnored();
};