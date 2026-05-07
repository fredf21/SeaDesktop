#include "access_control_config.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace sea::domain::access_control {

namespace {

std::string to_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

} // namespace anonyme

// ─── DefaultPolicy ───

std::string_view to_string(DefaultPolicy p) noexcept
{
    switch (p) {
    case DefaultPolicy::Deny:  return "deny";
    case DefaultPolicy::Allow: return "allow";
    }
    return "unknown";
}

std::optional<DefaultPolicy> default_policy_from_string(
    const std::string& s) noexcept
{
    const auto lower = to_lower(s);

    if (lower == "deny")  return DefaultPolicy::Deny;
    if (lower == "allow") return DefaultPolicy::Allow;

    return std::nullopt;
}

// ─── AccessControlConfig ───

bool AccessControlConfig::is_role_declared(const std::string& role) const noexcept
{
    if (declared_roles_.empty()) {
        // Si aucun rôle déclaré, on ne valide pas (mode permissif)
        return true;
    }
    return std::find(declared_roles_.begin(), declared_roles_.end(), role)
           != declared_roles_.end();
}

AccessControlConfig AccessControlConfig::disabled()
{
    AccessControlConfig cfg;
    cfg.enabled_ = false;
    return cfg;
}

AccessControlConfig AccessControlConfig::safe_defaults()
{
    AccessControlConfig cfg;
    cfg.enabled_ = true;
    cfg.default_policy_ = DefaultPolicy::Deny;
    cfg.roles_claim_name_ = "role";
    cfg.admin_role_ = "admin";
    cfg.default_allow_admin_ = true;
    return cfg;
}

ValidationResponse AccessControlConfig::validate() const
{
    ValidationResponse v;
    v.is_valid = true;
    v.message = "success";

    if (!enabled_) {
        v.is_valid = std::nullopt;
        v.message = std::string("rien a valider");
        return v;  // rien à valider si désactivé
    }

    if (roles_claim_name_.empty()) {
        v.is_valid = false;
        v.message = std::string("AccessControlConfig: roles_claim_name cannot be empty when enabled");
        return v;
    }

    if (admin_role_.empty()) {
        v.is_valid = false;
        v.message = std::string("AccessControlConfig: admin_role cannot be empty when enabled");
        return v;
    }

    // Si admin_role est défini mais pas dans la liste déclarée, on warn
    if (!declared_roles_.empty() && !is_role_declared(admin_role_)) {
        v.is_valid = false;
        v.message =             "AccessControlConfig: admin_role '" + admin_role_ +
                    "' is not in declared_roles list";
        return v;
    }
    return v;
}

} // namespace sea::domain::access_control