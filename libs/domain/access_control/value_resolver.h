#ifndef SEA_APPLICATION_ACCESS_CONTROL_VALUE_RESOLVER_H
#define SEA_APPLICATION_ACCESS_CONTROL_VALUE_RESOLVER_H

#include "access_control/policy_value_ref.h"
#include "access_control/policy_subject.h"
#include "access_control/policy_resource.h"
#include "access_control/policy_context.h"
#include "evaluation_options.h"

#include <optional>
#include <string>
#include <vector>

namespace sea::application::access_control {

/**
 * Une valeur résolue depuis un PolicyValueRef.
 *
 * Une valeur peut être :
 *   - scalaire (un seul string)
 *   - liste (vector<string>)
 *   - introuvable (les deux vides)
 *
 * On utilise std::string en interne pour TOUT (numbers, bools sont serialisés
 * en string). Les opérateurs feront la conversion (ex: GreaterThan parse en double).
 */
struct ResolvedValue {
    std::optional<std::string> scalar;
    std::optional<std::vector<std::string>> list;

    bool is_empty() const noexcept {
        return !scalar.has_value() && !list.has_value();
    }

    bool is_scalar() const noexcept { return scalar.has_value(); }
    bool is_list() const noexcept { return list.has_value(); }
};

/**
 * Résolveur de PolicyValueRef.
 *
 * Étant donné un PolicyValueRef (avec source + path), retourne la valeur
 * en naviguant dans subject / resource / context selon la source.
 *
 * Paths supportés :
 *   - "id"                          → champ direct
 *   - "roles"                       → liste
 *   - "email"                       → string
 *   - "attributes.department_id"    → navigation dans la map
 *   - "time.hour" (context only)    → calculé
 *   - "request.is_secure"           → calculé
 *
 * Erreurs :
 *   - Path inexistant + Strict mode  → throw std::runtime_error
 *   - Path inexistant + Permissive   → ResolvedValue vide
 */
class ValueResolver {
public:
    explicit ValueResolver(const EvaluationOptions& options);

    ResolvedValue resolve(
        const sea::domain::access_control::PolicyValueRef& ref,
        const sea::domain::access_control::PolicySubject& subject,
        const sea::domain::access_control::PolicyResource& resource,
        const sea::domain::access_control::PolicyContext& context
        ) const;

private:
    ResolvedValue resolve_literal(
        const sea::domain::access_control::PolicyValueRef& ref) const;

    ResolvedValue resolve_subject(
        const std::string& path,
        const sea::domain::access_control::PolicySubject& subject) const;

    ResolvedValue resolve_resource(
        const std::string& path,
        const sea::domain::access_control::PolicyResource& resource) const;

    ResolvedValue resolve_context(
        const std::string& path,
        const sea::domain::access_control::PolicyContext& context) const;

    // Cas du path introuvable selon le mode
    ResolvedValue handle_not_found(const std::string& full_path) const;

    EvaluationOptions options_;
};

} // namespace sea::application::access_control

#endif // SEA_APPLICATION_ACCESS_CONTROL_VALUE_RESOLVER_H