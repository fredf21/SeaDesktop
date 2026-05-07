#include "evaluation_options.h"

namespace sea::application::access_control {

EvaluationOptions EvaluationOptions::production()
{
    EvaluationOptions opts;
    opts.strict_mode = StrictMode::Permissive;   // ne crash pas en prod
    opts.detail_level = DetailLevel::BoolOnly;   // performance
    opts.short_circuit = true;
    return opts;
}

EvaluationOptions EvaluationOptions::staging()
{
    EvaluationOptions opts;
    opts.strict_mode = StrictMode::Permissive;
    opts.detail_level = DetailLevel::WithReason; // logs informatifs
    opts.short_circuit = true;
    return opts;
}

EvaluationOptions EvaluationOptions::development()
{
    EvaluationOptions opts;
    opts.strict_mode = StrictMode::Strict;        // catche les bugs vite
    opts.detail_level = DetailLevel::Verbose;     // tout afficher
    opts.short_circuit = false;                   // évaluer tout pour voir l'arbre complet
    return opts;
}

EvaluationOptions EvaluationOptions::pre_handler()
{
    EvaluationOptions opts;
    opts.strict_mode = StrictMode::Permissive;
    opts.detail_level = DetailLevel::WithReason;  // pour le 403 informatif
    opts.short_circuit = true;
    opts.ignore_resource_refs = true;             // ← clé : ignore les refs Resource
    return opts;
}

} // namespace sea::application::access_control