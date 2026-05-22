#pragma once

#include <QtTest/QtTest>
#include <QString>

#include <exception>
#include <type_traits>

// ─────────────────────────────────────────────────────────────
// TestJsonRecordParser
//
// Suite de tests du JsonRecordParser : convertit un body JSON
// en DynamicRecord en s'appuyant sur la définition d'une Entity.
//
// COMPORTEMENT IMPORTANT du code testé :
//   - Le parser n'extrait QUE les champs déclarés dans l'entité ;
//     les clés JSON inconnues sont ignorées.
//   - Un champ déclaré mais absent du JSON est simplement omis.
//   - Une valeur JSON null devient un std::monostate.
//   - Le parsing est STRICT : un champ dont le type JSON ne
//     correspond pas au FieldType déclaré fait échouer tout le
//     parse en levant une RUNTIME_EXECUTION (le champ n'est pas
//     omis silencieusement).
//   - Cas qui lèvent :
//       * JSON syntaxiquement invalide      -> RUNTIME_EXECUTION
//       * champ mal typé                    -> RUNTIME_EXECUTION
//       * body JSON qui n'est pas un objet  -> std::runtime_error
//
// Les tests vérifient la PRÉSENCE/ABSENCE des clés dans le record
// retourné, le type effectif de chaque valeur, et le rejet des
// champs mal typés.
//
// Organisation des slots :
//   1. Erreurs globales (JSON invalide, non-objet)
//   2. Extraction des champs déclarés / clés inconnues
//   3. Valeurs null -> monostate
//   4. Types scalaires (string, int, float, bool)
//   5. Entiers signed / unsigned
//   6. Champs mal typés -> exception (parsing strict)
// ─────────────────────────────────────────────────────────────
class TestJsonRecordParser : public QObject {
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
    // ── 1. Erreurs globales ───────────────────────────────────
    void parse_invalidJson_shouldThrow();
    void parse_emptyString_shouldThrow();
    void parse_jsonArray_shouldThrow();
    void parse_jsonScalar_shouldThrow();

    // ── 2. Extraction des champs déclarés / clés inconnues ────
    void parse_emptyObject_shouldReturnEmptyRecord();
    void parse_declaredFields_shouldBeExtracted();
    void parse_unknownJsonKeys_shouldBeIgnored();
    void parse_declaredFieldAbsentFromJson_shouldBeOmitted();

    // ── 3. Valeurs null -> monostate ──────────────────────────
    void parse_nullValue_shouldBecomeMonostate();
    void parse_nullValueForRequiredField_shouldStillBecomeMonostate();

    // ── 4. Types scalaires ────────────────────────────────────
    void parse_stringField_shouldStoreString();
    void parse_floatField_shouldStoreDouble();
    void parse_floatField_acceptsInteger();
    void parse_boolField_shouldStoreBool();
    void parse_jsonField_shouldStoreJson();
    void parse_jsonField_acceptsScalarValue();
    void parse_jsonField_acceptsArrayValue();
    void parse_uuidAndEmailFields_shouldStoreString();

    // ── 5. Entiers signed / unsigned ──────────────────────────
    void parse_intField_shouldStoreInt32();
    void parse_bigIntField_shouldStoreInt64();
    void parse_smallIntField_shouldStoreInt16();
    void parse_unsignedIntField_shouldStoreUint32();
    void parse_unsignedBigIntField_shouldStoreUint64();
    void parse_unsignedSmallIntField_shouldStoreUint16();

    // ── 5b. Champ Binary (base64) ─────────────────────────────
    void parse_binaryField_shouldDecodeBase64();
    void parse_binaryField_emptyString_shouldStoreEmptyVector();
    void parse_binaryFieldWithNonStringValue_shouldThrow();

    // ── 5c. Dépassement de plage des entiers ──────────────────
    // Le contrôle de plage compare la valeur lue en int64_t aux
    // bornes du type cible AVANT conversion. Une valeur hors plage
    // (ou négative sur un type non signé) lève une RUNTIME_EXECUTION.
    void parse_smallIntOutOfRange_shouldThrow();
    void parse_smallIntAtUpperBound_shouldPass();
    void parse_unsignedSmallIntNegative_shouldThrow();
    void parse_unsignedSmallIntOutOfRange_shouldThrow();
    void parse_intOutOfRange_shouldThrow();
    void parse_intAtUpperBound_shouldPass();
    void parse_unsignedIntNegative_shouldThrow();
    void parse_unsignedBigIntNegative_shouldThrow();

    // ── 6. Champs mal typés -> exception (parsing strict) ─────
    // Depuis la correction du catch interne, un champ dont le type
    // JSON ne correspond pas au FieldType déclaré fait échouer tout
    // le parsing en levant une RUNTIME_EXECUTION.
    void parse_stringFieldWithIntValue_shouldThrow();
    void parse_intFieldWithStringValue_shouldThrow();
    void parse_boolFieldWithStringValue_shouldThrow();
    void parse_floatFieldWithStringValue_shouldThrow();
    void parse_invalidFieldAbortsWholeParse();
};