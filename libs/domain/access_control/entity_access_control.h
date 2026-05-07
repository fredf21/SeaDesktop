#ifndef SEA_DOMAIN_ACCESS_CONTROL_ENTITY_ACCESS_CONTROL_H
#define SEA_DOMAIN_ACCESS_CONTROL_ENTITY_ACCESS_CONTROL_H

#include "access_control_spec.h"
#include "crud_operation.h"
#include "security_scheme/abac_mode.h"

#include <map>
#include <optional>
#include <string>

namespace sea::domain::access_control {

/**
 * Règles d'accès pour TOUTES les opérations d'UNE entité.
 *
 * Chaque entité peut définir des règles spécifiques par opération
 * (list, get_by_id, create, update, delete).
 *
 * Configuration au niveau entité (utilisée par les shortcuts) :
 *   - scope_field : nom du champ utilisé par `same_scope: true`
 *                   (ex: "department_id" pour Employee, "team_id" pour Project)
 *   - owner_field : nom du champ identifiant le propriétaire,
 *                   utilisé par `own_resource: true`
 *                   (ex: "id" pour User, "owner_id" pour Document)
 *   - abac_mode_override : ( override du mode ABAC defini au
 *                          niveau service pour cette entite specifique.
 *                          Permet par exemple d'avoir une entite en
 *                          "strict" pendant que le reste du service
 *                          est en "permissive".
 *
 * Si une opération n'a pas de spec définie, le moteur applique
 * la default_policy globale (deny ou allow selon AccessControlConfig).
 */
class EntityAccessControl {
public:
    EntityAccessControl() = default;

    // ─── Specs par opération ───

    void set_spec(CrudOperation op, AccessControlSpec spec);

    // Retourne la spec pour une opération, ou nullopt si non définie
    const AccessControlSpec* find_spec(CrudOperation op) const;

    bool has_spec(CrudOperation op) const;

    bool has_any_spec() const noexcept { return !specs_.empty(); }

    // ─── Configuration des shortcuts ───

    const std::string& scope_field() const noexcept { return scope_field_; }
    void set_scope_field(std::string field) { scope_field_ = std::move(field); }

    const std::string& owner_field() const noexcept { return owner_field_; }
    void set_owner_field(std::string field) { owner_field_ = std::move(field); }

    // ─── Mode ABAC override ───

    /**
     * Override du mode ABAC pour cette entite specifique.
     *
     * Si non defini (nullopt), le mode du service AccessControlConfig
     * est utilise.
     *
     * Exemple YAML :
     *   - name: AuditLog
     *     access_control:
     *       abac_mode: strict      # Override : cette entite uniquement
     *       update:
     *         allow_roles: [admin]
     *         same_scope: true     # → bloquera 403 (cause strict)
     */
    std::optional<AbacMode> abac_mode_override() const noexcept {
        return abac_mode_override_;
    }
    void set_abac_mode_override(AbacMode mode) {
        abac_mode_override_ = mode;
    }
    void clear_abac_mode_override() noexcept {
        abac_mode_override_ = std::nullopt;
    }

    // ─── Validation ───

    void validate() const;

private:
    std::map<CrudOperation, AccessControlSpec> specs_;
    std::string scope_field_;   // ex: "department_id"
    std::string owner_field_;   // ex: "id" pour User, "user_id" pour Document

    // override optionnel du mode ABAC pour cette entite
    std::optional<AbacMode> abac_mode_override_;
};

} // namespace sea::domain::access_control

#endif // SEA_DOMAIN_ACCESS_CONTROL_ENTITY_ACCESS_CONTROL_H