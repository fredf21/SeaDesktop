#ifndef SEA_DOMAIN_ACCESS_CONTROL_ACCESS_CONTROL_CONFIG_H
#define SEA_DOMAIN_ACCESS_CONTROL_ACCESS_CONTROL_CONFIG_H

#include "security_scheme/abac_mode.h"

#include <optional>
#include <string>
#include <vector>

namespace sea::domain::access_control {

/**
 * Comportement par défaut quand une entité n'a pas de spec d'accès.
 *
 * Deny  : refuse l'accès (sécurisé par défaut)
 * Allow : autorise tout user authentifié
 */
enum class DefaultPolicy {
    Deny,
    Allow
};

std::string_view to_string(DefaultPolicy p) noexcept;
std::optional<DefaultPolicy> default_policy_from_string(
    const std::string& s) noexcept;


/**
 * reponse pour la validation
 */
struct ValidationResponse{
    std::string message;
    std::optional<bool> is_valid;
};

/**
 * Configuration globale du système d'autorisation pour un service.
 *
 * Cette config est lue depuis le YAML sous `security.authorization`.
 * Elle pilote :
 *   - Si l'autorisation est activée
 *   - Le comportement par défaut (deny/allow)
 *   - Le nom du claim JWT contenant le rôle
 *   - Le rôle qui bypasse les ABAC checks (admin)
 *   - Le champ par défaut pour `same_scope: true` shortcut
 *   - Le catalogue des rôles déclarés (validation)
 *   - Le mode ABAC pour les regles resource-aware (permissive/strict)
 */
class AccessControlConfig {
public:
    AccessControlConfig() = default;

    // ─── Activation ───

    bool enabled() const noexcept { return enabled_; }
    void set_enabled(bool v) { enabled_ = v; }

    // ─── Politique par défaut ───

    DefaultPolicy default_policy() const noexcept { return default_policy_; }
    void set_default_policy(DefaultPolicy p) { default_policy_ = p; }

    // ─── Configuration JWT ───

    const std::string& roles_claim_name() const noexcept {
        return roles_claim_name_;
    }
    void set_roles_claim_name(std::string n) {
        roles_claim_name_ = std::move(n);
    }

    // ─── Rôle admin (bypass) ───

    const std::string& admin_role() const noexcept { return admin_role_; }
    void set_admin_role(std::string r) { admin_role_ = std::move(r); }

    bool default_allow_admin() const noexcept { return default_allow_admin_; }
    void set_default_allow_admin(bool v) { default_allow_admin_ = v; }

    // ─── Shortcuts ───

    // Champ de scope par défaut (ex: "department_id")
    // Utilisé quand une entité n'a pas son propre scope_field
    const std::string& default_scope_field() const noexcept {
        return default_scope_field_;
    }
    void set_default_scope_field(std::string f) {
        default_scope_field_ = std::move(f);
    }

    // ─── Catalogue des rôles ───

    const std::vector<std::string>& declared_roles() const noexcept {
        return declared_roles_;
    }
    void add_declared_role(std::string role) {
        declared_roles_.push_back(std::move(role));
    }
    void set_declared_roles(std::vector<std::string> roles) {
        declared_roles_ = std::move(roles);
    }

    bool is_role_declared(const std::string& role) const noexcept;

    // ─── Mode ABAC ───

    /**
     * Mode d'evaluation des regles ABAC resource-aware au niveau service.
     * Defaut : Permissive (compatible avec own_resource et same_scope).
     * Peut etre override au niveau entite.
     */
    AbacMode abac_mode() const noexcept { return abac_mode_; }
    void set_abac_mode(AbacMode mode) { abac_mode_ = mode; }

    // ─── Factories ───

    static AccessControlConfig disabled();
    static AccessControlConfig safe_defaults();

    // ─── Validation ───

    ValidationResponse validate() const;

private:
    bool enabled_ = false;
    DefaultPolicy default_policy_ = DefaultPolicy::Deny;
    std::string roles_claim_name_ = "role";
    std::string admin_role_ = "admin";
    bool default_allow_admin_ = true;
    std::string default_scope_field_;
    std::vector<std::string> declared_roles_;

    // mode ABAC (defaut permissive pour compatibilite)
    AbacMode abac_mode_ = AbacMode::Permissive;
};

} // namespace sea::domain::access_control

#endif // SEA_DOMAIN_ACCESS_CONTROL_ACCESS_CONTROL_CONFIG_H