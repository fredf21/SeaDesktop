#ifndef SEA_DOMAIN_ACCESS_CONTROL_POLICY_RESOURCE_H
#define SEA_DOMAIN_ACCESS_CONTROL_POLICY_RESOURCE_H

#include <optional>
#include <string>
#include <unordered_map>

namespace sea::domain::access_control {

/**
 * La ressource d'une requête : SUR QUOI on demande l'accès.
 *
 * Pour les opérations CRUD :
 *   - List      : pas de ressource spécifique (resource est vide)
 *   - GetById   : la ressource = l'item retourné par la DB
 *   - Create    : la ressource = le payload (body de la requête)
 *   - Update    : la ressource = l'item AVANT modification (chargé pour ABAC check)
 *   - Delete    : la ressource = l'item à supprimer (chargé pour ABAC check)
 *
 * Champs structurés :
 *   - entity_name : nom de l'entité (ex: "Employee")
 *   - id          : identifiant de l'instance
 *
 * Champs additionnels :
 *   - attributes  : tous les autres champs de la ressource (depuis JSON DB)
 *                   ex: {"name": "David", "department_id": "dept_IT", "salary": "50000"}
 *
 * Exemple d'accès via path :
 *   "entity_name"                   → entity_name
 *   "id"                            → id
 *   "attributes.department_id"      → attributes["department_id"]
 *   "attributes.owner_id"           → attributes["owner_id"]
 */
struct PolicyResource {
    std::string entity_name;
    std::string id;
    std::unordered_map<std::string, std::string> attributes;

    bool is_empty() const noexcept
    {
        return entity_name.empty() && id.empty() && attributes.empty();
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

#endif // SEA_DOMAIN_ACCESS_CONTROL_POLICY_RESOURCE_H