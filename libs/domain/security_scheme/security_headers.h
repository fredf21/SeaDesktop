#pragma once
// sea_domain/security_scheme/security_headers.h

#include <optional>
#include <string>

namespace sea::domain::security {

class SecurityHeaders {
public:
    // Constructeur par défaut : aucun header (l'utilisateur opt-in)
    SecurityHeaders() = default;

    // Builder fluide
    SecurityHeaders& set_hsts(std::string value);
    SecurityHeaders& disable_hsts();

    SecurityHeaders& set_content_type_options(std::string value);
    SecurityHeaders& disable_content_type_options();

    SecurityHeaders& set_frame_options(std::string value);
    SecurityHeaders& disable_frame_options();

    SecurityHeaders& set_referrer_policy(std::string value);
    SecurityHeaders& disable_referrer_policy();

    SecurityHeaders& set_content_security_policy(std::string value);
    SecurityHeaders& disable_content_security_policy();

    SecurityHeaders& set_permissions_policy(std::string value);
    SecurityHeaders& disable_permissions_policy();

    SecurityHeaders& set_cross_origin_opener_policy(std::string value);
    SecurityHeaders& disable_cross_origin_opener_policy();

    SecurityHeaders& set_cross_origin_resource_policy(std::string value);
    SecurityHeaders& disable_cross_origin_resource_policy();

    // Accesseurs : nullopt = header désactivé
    const std::optional<std::string>& hsts() const;
    const std::optional<std::string>& content_type_options() const;
    const std::optional<std::string>& frame_options() const;
    const std::optional<std::string>& referrer_policy() const;
    const std::optional<std::string>& content_security_policy() const;
    const std::optional<std::string>& permissions_policy() const;
    const std::optional<std::string>& cross_origin_opener_policy() const;
    const std::optional<std::string>& cross_origin_resource_policy() const;

    // Factory : configuration recommandée OWASP
    static SecurityHeaders recommended();

    // Factory : configuration stricte (max sécurité, peut casser certains usages)
    static SecurityHeaders strict();

    // Factory : aucun header (pour les services derrière un proxy qui les gère)
    static SecurityHeaders none();

private:
    std::optional<std::string> hsts_;
    std::optional<std::string> content_type_options_;
    std::optional<std::string> frame_options_;
    std::optional<std::string> referrer_policy_;
    std::optional<std::string> content_security_policy_;
    std::optional<std::string> permissions_policy_;
    std::optional<std::string> cross_origin_opener_policy_;
    std::optional<std::string> cross_origin_resource_policy_;
};

} // namespace sea::domain::security