#ifndef SEA_APPLICATION_ACCESS_CONTROL_EVALUATION_OPTIONS_H
#define SEA_APPLICATION_ACCESS_CONTROL_EVALUATION_OPTIONS_H

namespace sea::application::access_control {

/**
 * Mode de stricte du moteur quand un path est introuvable.
 *
 * Strict :
 *   - Path "subject.attributes.foo" inexistant → throw std::runtime_error
 *   - Recommandé en dev pour catcher les fautes de frappe dans le YAML
 *
 * Permissive :
 *   - Path inexistant → la valeur résolue est nullopt
 *   - Le prédicat évalue à FALSE (sauf NotExists qui évalue TRUE)
 *   - Recommandé en prod pour ne pas crasher sur un YAML malformé
 */
enum class StrictMode {
    Strict,
    Permissive
};

/**
 * Niveau de détail dans le résultat d'évaluation.
 *
 * BoolOnly :
 *   - Le résultat ne contient QUE le bool allowed
 *   - Ultra-rapide, aucun overhead
 *   - Recommandé en prod
 *
 * WithReason :
 *   - Inclut une explication "Failed: subject.role doesn't contain admin"
 *   - Léger overhead (allocation d'une string)
 *   - Recommandé en staging
 *
 * Verbose :
 *   - Inclut les valeurs résolues, le détail de l'arbre, les chemins évalués
 *   - Overhead non négligeable (multiples allocations)
 *   - Recommandé UNIQUEMENT en dev / debug
 */
enum class DetailLevel {
    BoolOnly,
    WithReason,
    Verbose
};

/**
 * Options pour une évaluation de PolicyCondition.
 *
 * Configuration courante en prod :
 *   { Permissive, BoolOnly }
 *
 * Configuration courante en dev :
 *   { Strict, Verbose }
 */
struct EvaluationOptions {
    StrictMode strict_mode = StrictMode::Permissive;
    DetailLevel detail_level = DetailLevel::BoolOnly;

    // Court-circuiter les opérateurs logiques (true = optimisation, recommandé)
    bool short_circuit = true;

    // Si true, l'évaluation ignore les prédicats qui référencent Resource.
    // Utilisé pour le pré-check pré-handler.
    bool ignore_resource_refs = false;

    // Factories courantes
    static EvaluationOptions production();
    static EvaluationOptions staging();
    static EvaluationOptions development();
    static EvaluationOptions pre_handler();
};

} // namespace sea::application::access_control

#endif // SEA_APPLICATION_ACCESS_CONTROL_EVALUATION_OPTIONS_H