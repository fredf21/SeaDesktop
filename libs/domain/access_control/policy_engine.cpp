#include "policy_engine.h"

#include <sstream>

namespace sea::application::access_control {

using namespace sea::domain::access_control;

PolicyEngine::PolicyEngine(
    const OperatorRegistry& registry)
    : registry_(registry)
{
}

// ──────────────────────────────────────────────────────────
// Évaluation publique
// ──────────────────────────────────────────────────────────

EvaluationResult PolicyEngine::evaluate(
    const PolicyCondition& condition,
    const PolicySubject& subject,
    const PolicyResource& resource,
    const PolicyContext& context,
    const EvaluationOptions& options) const
{
    EvaluationResult result;
    result.allowed = evaluate_condition(
        condition, subject, resource, context, options, result
        );

    // Si refusé et pas encore de raison, on en met une générique
    if (!result.allowed && options.detail_level >= DetailLevel::WithReason
        && !result.reason.has_value()) {
        result.reason = "Access denied by policy condition";
    }

    return result;
}

EvaluationResult PolicyEngine::evaluate_subject_only(
    const PolicyCondition& condition,
    const PolicySubject& subject,
    const PolicyContext& context,
    const EvaluationOptions& options) const
{
    // Crée un Resource vide
    PolicyResource empty_resource;

    // Force ignore_resource_refs dans les options
    EvaluationOptions opts = options;
    opts.ignore_resource_refs = true;

    return evaluate(condition, subject, empty_resource, context, opts);
}

// ──────────────────────────────────────────────────────────
// Évaluation récursive
// ──────────────────────────────────────────────────────────

bool PolicyEngine::evaluate_condition(
    const PolicyCondition& condition,
    const PolicySubject& subject,
    const PolicyResource& resource,
    const PolicyContext& context,
    const EvaluationOptions& options,
    EvaluationResult& result) const
{
    switch (condition.type()) {
    case PolicyConditionType::Predicate: {
        if (!condition.predicate().has_value()) {
            return false;
        }
        const auto& pred = *condition.predicate();

        // Si on est en mode pre-handler ET que ce prédicat dépend de Resource,
        // on le considère comme TRUE (sera réévalué post-handler)
        if (options.ignore_resource_refs && predicate_uses_resource(pred)) {
            return true;
        }

        return evaluate_predicate(pred, subject, resource, context, options, result);
    }

    case PolicyConditionType::All: {
        // AND : tous doivent être TRUE
        // Avec short_circuit : on s'arrête au premier FALSE
        bool all_true = true;
        for (const auto& child : condition.children()) {
            const bool child_result =
                evaluate_condition(child, subject, resource, context, options, result);
            if (!child_result) {
                all_true = false;
                if (options.short_circuit) {
                    return false;
                }
            }
        }
        return all_true;
    }

    case PolicyConditionType::Any: {
        // OR : au moins un doit être TRUE
        // Avec short_circuit : on s'arrête au premier TRUE
        bool any_true = false;
        for (const auto& child : condition.children()) {
            const bool child_result =
                evaluate_condition(child, subject, resource, context, options, result);
            if (child_result) {
                any_true = true;
                if (options.short_circuit) {
                    return true;
                }
            }
        }
        return any_true;
    }

    case PolicyConditionType::Not: {
        // NOT : inverse l'enfant unique
        if (condition.children().empty()) {
            return false;
        }
        return !evaluate_condition(
            condition.children()[0], subject, resource, context, options, result
            );
    }
    }
    return false;
}

bool PolicyEngine::evaluate_predicate(
    const PolicyPredicate& predicate,
    const PolicySubject& subject,
    const PolicyResource& resource,
    const PolicyContext& context,
    const EvaluationOptions& options,
    EvaluationResult& result) const
{
    ValueResolver resolver(options);
    OperatorEvaluator op_evaluator(registry_);

    ResolvedValue left;
    ResolvedValue right;

    try {
        left = resolver.resolve(predicate.left, subject, resource, context);
        right = resolver.resolve(predicate.right, subject, resource, context);
    } catch (const std::exception& e) {
        // Strict mode : a throw pendant la résolution
        if (options.detail_level >= DetailLevel::WithReason) {
            result.reason = std::string("Resolution error: ") + e.what();
        }
        return false;
    }

    const bool predicate_result =
        op_evaluator.evaluate(predicate.op, left, right);

    ++result.predicates_evaluated;

    // Trace verbose
    if (options.detail_level == DetailLevel::Verbose) {
        PredicateTrace trace;
        trace.description = format_predicate(predicate);
        trace.left_resolved = format_value(left);
        trace.right_resolved = format_value(right);
        trace.result = predicate_result;
        result.traces.push_back(std::move(trace));
    }

    // Reason : si refus et qu'on n'a pas encore de raison, on en met une
    if (!predicate_result
        && options.detail_level >= DetailLevel::WithReason
        && !result.reason.has_value()) {
        result.reason = "Failed: " + format_predicate(predicate);
    }

    return predicate_result;
}

bool PolicyEngine::predicate_uses_resource(
    const PolicyPredicate& predicate) const noexcept
{
    return predicate.left.source == PolicyValueSource::Resource
           || predicate.right.source == PolicyValueSource::Resource;
}

// ──────────────────────────────────────────────────────────
// Helpers de formatage pour les traces / logs
// ──────────────────────────────────────────────────────────

std::string PolicyEngine::format_predicate(const PolicyPredicate& p) const
{
    std::ostringstream oss;

    auto format_ref = [](const PolicyValueRef& ref) -> std::string {
        switch (ref.source) {
        case PolicyValueSource::Literal:
            if (!ref.literal_list.empty()) {
                std::string s = "[";
                for (std::size_t i = 0; i < ref.literal_list.size(); ++i) {
                    if (i > 0) s += ", ";
                    s += ref.literal_list[i];
                }
                s += "]";
                return s;
            }
            return "'" + ref.literal + "'";
        case PolicyValueSource::Subject:
            return "subject." + ref.path;
        case PolicyValueSource::Resource:
            return "resource." + ref.path;
        case PolicyValueSource::Context:
            return "context." + ref.path;
        }
        return "?";
    };

    oss << format_ref(p.left)
        << " " << to_string(p.op) << " "
        << format_ref(p.right);
    return oss.str();
}

std::string PolicyEngine::format_value(const ResolvedValue& v) const
{
    if (v.is_empty()) return "<empty>";
    if (v.is_scalar()) return "'" + *v.scalar + "'";
    if (v.is_list()) {
        std::string s = "[";
        for (std::size_t i = 0; i < v.list->size(); ++i) {
            if (i > 0) s += ", ";
            s += "'" + (*v.list)[i] + "'";
        }
        s += "]";
        return s;
    }
    return "?";
}

} // namespace sea::application::access_control