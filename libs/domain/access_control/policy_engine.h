#ifndef SEA_APPLICATION_ACCESS_CONTROL_POLICY_ENGINE_H
#define SEA_APPLICATION_ACCESS_CONTROL_POLICY_ENGINE_H

#include "access_control/policy_condition.h"
#include "access_control/policy_subject.h"
#include "access_control/policy_resource.h"
#include "access_control/policy_context.h"
#include "evaluation_options.h"
#include "evaluation_result.h"
#include "operator_evaluator.h"
#include "value_resolver.h"

#include <memory>
#include <regex>
#include <string>
#include <unordered_map>

namespace sea::application::access_control {

/**
 * Le moteur d'évaluation principal.
 *
 * Construction :
 *   - regex_cache : compilé par PolicyCompiler::compile_all() au boot
 *
 * Usage :
 *   PolicyEngine engine(regex_cache);
 *   auto result = engine.evaluate(condition, subject, resource, context, options);
 *   if (result.allowed) { ... }
 */
class PolicyEngine {
public:
    explicit PolicyEngine(
        const OperatorRegistry& registry
        );

    /**
     * Évalue une PolicyCondition complète.
     *
     * Retourne un EvaluationResult dont le contenu dépend de
     * options.detail_level :
     *   - BoolOnly   : juste `allowed`
     *   - WithReason : + `reason` si refus
     *   - Verbose    : + traces de chaque prédicat
     */
    EvaluationResult evaluate(
        const sea::domain::access_control::PolicyCondition& condition,
        const sea::domain::access_control::PolicySubject& subject,
        const sea::domain::access_control::PolicyResource& resource,
        const sea::domain::access_control::PolicyContext& context,
        const EvaluationOptions& options = EvaluationOptions::production()
        ) const;

    /**
     * Évalue UNIQUEMENT les sous-conditions qui ne dépendent pas de Resource.
     *
     * Utilisé pour le pré-check pré-handler :
     *   - Si retourne FALSE → 403 immédiat, pas besoin de DB
     *   - Si retourne TRUE  → continuer dans le handler, qui chargera la
     *     ressource puis fera evaluate() complet
     *
     * Stratégie :
     *   - Si la condition entière ne dépend pas de Resource : évaluation normale
     *   - Si la condition contient des refs Resource : évalue avec un Resource
     *     vide et options.ignore_resource_refs = true (les prédicats Resource
     *     sont considérés comme TRUE pour ne pas court-circuiter à tort)
     */
    EvaluationResult evaluate_subject_only(
        const sea::domain::access_control::PolicyCondition& condition,
        const sea::domain::access_control::PolicySubject& subject,
        const sea::domain::access_control::PolicyContext& context,
        const EvaluationOptions& options = EvaluationOptions::pre_handler()
        ) const;

private:
    bool evaluate_condition(
        const sea::domain::access_control::PolicyCondition& condition,
        const sea::domain::access_control::PolicySubject& subject,
        const sea::domain::access_control::PolicyResource& resource,
        const sea::domain::access_control::PolicyContext& context,
        const EvaluationOptions& options,
        EvaluationResult& result
        ) const;

    bool evaluate_predicate(
        const sea::domain::access_control::PolicyPredicate& predicate,
        const sea::domain::access_control::PolicySubject& subject,
        const sea::domain::access_control::PolicyResource& resource,
        const sea::domain::access_control::PolicyContext& context,
        const EvaluationOptions& options,
        EvaluationResult& result
        ) const;

    bool predicate_uses_resource(
        const sea::domain::access_control::PolicyPredicate& predicate
        ) const noexcept;

    std::string format_predicate(
        const sea::domain::access_control::PolicyPredicate& predicate
        ) const;

    std::string format_value(const ResolvedValue& v) const;

    const OperatorRegistry& registry_;
};

} // namespace sea::application::access_control

#endif // SEA_APPLICATION_ACCESS_CONTROL_POLICY_ENGINE_H