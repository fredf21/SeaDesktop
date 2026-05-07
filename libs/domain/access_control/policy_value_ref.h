#ifndef SEA_DOMAIN_ACCESS_CONTROL_POLICY_VALUE_REF_H
#define SEA_DOMAIN_ACCESS_CONTROL_POLICY_VALUE_REF_H

#include "policy_value_source.h"

#include <string>
#include <vector>

namespace sea::domain::access_control {

/**
 * Référence vers une valeur utilisée dans une condition.
 *
 * Selon la source :
 *   - Literal  : utiliser `literal` (string) ou `literal_list` (vector<string>)
 *   - Subject  : utiliser `path` pour adresser un champ (ex: "roles", "attributes.dept_id")
 *   - Resource : idem, ex: "attributes.owner_id"
 *   - Context  : idem, ex: "time.hour", "request.ip"
 *
 * Le `path` utilise une notation à points. Le moteur d'évaluation
 * navigue dans les structures pour résoudre la valeur.
 *
 * Exemples YAML :
 *   { source: literal, literal: "admin" }
 *   { source: literal, literal_list: [admin, manager] }
 *   { source: subject, path: "roles" }
 *   { source: subject, path: "attributes.department_id" }
 *   { source: resource, path: "attributes.owner_id" }
 *   { source: context, path: "time.hour" }
 */
struct PolicyValueRef {
    PolicyValueSource source = PolicyValueSource::Literal;

    // Pour Subject/Resource/Context : chemin d'accès (ex: "attributes.dept_id")
    std::string path;

    // Pour Literal scalaire (string, number serialisé, bool serialisé)
    std::string literal;

    // Pour Literal liste (utilisé avec In, NotIn, Intersects)
    std::vector<std::string> literal_list;

    // ─── Factories utiles ───

    static PolicyValueRef from_subject(std::string path)
    {
        PolicyValueRef ref;
        ref.source = PolicyValueSource::Subject;
        ref.path = std::move(path);
        return ref;
    }

    static PolicyValueRef from_resource(std::string path)
    {
        PolicyValueRef ref;
        ref.source = PolicyValueSource::Resource;
        ref.path = std::move(path);
        return ref;
    }

    static PolicyValueRef from_context(std::string path)
    {
        PolicyValueRef ref;
        ref.source = PolicyValueSource::Context;
        ref.path = std::move(path);
        return ref;
    }

    static PolicyValueRef from_literal(std::string value)
    {
        PolicyValueRef ref;
        ref.source = PolicyValueSource::Literal;
        ref.literal = std::move(value);
        return ref;
    }

    static PolicyValueRef from_literal_list(std::vector<std::string> values)
    {
        PolicyValueRef ref;
        ref.source = PolicyValueSource::Literal;
        ref.literal_list = std::move(values);
        return ref;
    }
};

} // namespace sea::domain::access_control

#endif // SEA_DOMAIN_ACCESS_CONTROL_POLICY_VALUE_REF_H