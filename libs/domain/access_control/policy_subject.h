#ifndef SEA_DOMAIN_ACCESS_CONTROL_POLICY_SUBJECT_H
#define SEA_DOMAIN_ACCESS_CONTROL_POLICY_SUBJECT_H

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sea::domain::access_control {

/**
 * Le sujet d'une requête : QUI demande l'accès.
 *
 * Construit à partir des claims du JWT par AuthMiddleware.
 *
 * Champs structurés :
 *   - id         : identifiant unique (claim "sub")
 *   - email      : email du user (claim "email")
 *   - roles      : liste des rôles (claim "roles" ou ["role"] si scalaire)
 *
 * Champs additionnels :
 *   - attributes : map clé/valeur des autres claims
 *                  ex: {"department_id": "dept_IT", "tenant_id": "t1", "mfa_verified": "true"}
 *
 * Exemple d'accès via path :
 *   "id"                            → id
 *   "email"                         → email
 *   "roles"                         → roles (pour contains, in, intersects)
 *   "attributes.department_id"      → attributes["department_id"]
 *   "attributes.mfa_verified"       → attributes["mfa_verified"]
 */
struct PolicySubject {
    std::string id;
    std::string email;
    std::vector<std::string> roles;
    std::unordered_map<std::string, std::string> attributes;

    // Helpers
    bool has_role(const std::string& role) const noexcept
    {
        for (const auto& r : roles) {
            if (r == role) return true;
        }
        return false;
    }

    std::optional<std::string> get_attribute(const std::string& key) const
    {
        auto it = attributes.find(key);
        if (it == attributes.end()) {
            return std::nullopt;
        }
        return it->second;
    }
};

} // namespace sea::domain::access_control

#endif // SEA_DOMAIN_ACCESS_CONTROL_POLICY_SUBJECT_H