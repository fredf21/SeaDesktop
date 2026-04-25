#pragma once
// sea_domain/security_scheme/rate_limit_rule.h

#include <chrono>
#include <cstdint>
#include <string_view>

namespace sea::domain::security {

// À qui s'applique la règle de rate limiting
enum class RateLimitScope {
    PerIp,        // par adresse IP source
    PerUser,      // par utilisateur authentifié (UserId)
    PerApiKey,    // par clé API (X-API-Key)
    Global        // pour le serveur entier
};

// Conversion enum <-> string pour le YAML
constexpr std::string_view to_string(RateLimitScope s) noexcept
{
    switch (s) {
    case RateLimitScope::PerIp:     return "per_ip";
    case RateLimitScope::PerUser:   return "per_user";
    case RateLimitScope::PerApiKey: return "per_api_key";
    case RateLimitScope::Global:    return "global";
    }
    return "unknown";
}

RateLimitScope scope_from_string(std::string_view s);

class RateLimitRule {
public:
    // Constructeur par défaut : règle "désactivée" (requests=0)
    // À utiliser uniquement comme placeholder, ne pas l'envoyer au runtime
    RateLimitRule();

    // Constructeur principal : tous les champs requis
    RateLimitRule(RateLimitScope scope,
                  std::uint32_t requests,
                  std::chrono::seconds window,
                  std::uint32_t burst);

    // Builder fluide (pour usage programmatique)
    RateLimitRule& set_scope(RateLimitScope scope);
    RateLimitRule& set_requests(std::uint32_t requests);
    RateLimitRule& set_window(std::chrono::seconds window);
    RateLimitRule& set_burst(std::uint32_t burst);

    // Accesseurs
    RateLimitScope scope() const;
    std::uint32_t requests() const;
    std::chrono::seconds window() const;
    std::uint32_t burst() const;

    // Calcule le taux de remplissage du token bucket en tokens/seconde
    // Utile pour l'implémentation du middleware
    double refill_rate_per_second() const;

    // Vérifie que la règle est cohérente
    // Lance std::invalid_argument si invalide
    void validate() const;

    // Factories pour les cas usuels
    static RateLimitRule per_ip(std::uint32_t requests,
                                std::chrono::seconds window,
                                std::uint32_t burst);

    static RateLimitRule per_user(std::uint32_t requests,
                                  std::chrono::seconds window,
                                  std::uint32_t burst);

    static RateLimitRule global(std::uint32_t requests,
                                std::chrono::seconds window,
                                std::uint32_t burst);

private:
    RateLimitScope scope_;
    std::uint32_t requests_;
    std::chrono::seconds window_;
    std::uint32_t burst_;
};

} // namespace sea::domain::security