#ifndef SEA_APPLICATION_ACCESS_CONTROL_EVALUATION_RESULT_H
#define SEA_APPLICATION_ACCESS_CONTROL_EVALUATION_RESULT_H

#include <optional>
#include <string>
#include <vector>

namespace sea::application::access_control {

/**
 * Détail d'un prédicat évalué (utilisé dans DetailLevel::Verbose).
 *
 * Permet de tracer exactement ce qui s'est passé pour chaque comparaison.
 */
struct PredicateTrace {
    std::string description;        // ex: "subject.roles contains 'admin'"
    std::string left_resolved;      // valeur résolue de left (ex: "['user']")
    std::string right_resolved;     // valeur résolue de right (ex: "'admin'")
    bool result = false;
    std::optional<std::string> error;  // si une erreur s'est produite
};

/**
 * Résultat d'une évaluation de PolicyCondition.
 *
 * Contenu selon DetailLevel :
 *   - BoolOnly   : seul `allowed` est rempli, autres champs vides
 *   - WithReason : allowed + reason (string explicite si refusé)
 *   - Verbose    : tout, y compris la trace de chaque prédicat
 *
 * En prod, on accède juste à `allowed`.
 * En dev, on logge tout pour comprendre les refus.
 */
struct EvaluationResult {
    bool allowed = false;

    // Rempli si DetailLevel >= WithReason
    std::optional<std::string> reason;

    // Rempli si DetailLevel == Verbose
    std::vector<PredicateTrace> traces;

    // Compteurs pour métriques (toujours remplis)
    std::size_t predicates_evaluated = 0;

    // Helper pour générer un message clair pour les logs / réponses HTTP
    std::string format_for_log() const;
};

} // namespace sea::application::access_control

#endif // SEA_APPLICATION_ACCESS_CONTROL_EVALUATION_RESULT_H