#ifndef SEA_DOMAIN_ACCESS_CONTROL_POLICY_CONTEXT_H
#define SEA_DOMAIN_ACCESS_CONTROL_POLICY_CONTEXT_H

#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>

namespace sea::domain::access_control {

/**
 * Le contexte d'une requête : DANS QUELLES CONDITIONS l'accès est demandé.
 *
 * Construit par AuthorizationMiddleware à partir de la requête HTTP
 * et du temps système.
 *
 * Champs structurés :
 *   - method  : verbe HTTP (GET, POST, ...)
 *   - path    : chemin de la requête (/employees/42)
 *   - ip      : IP du client (extraite de X-Forwarded-For ou direct)
 *   - now     : timestamp UTC
 *
 * Attributs dérivés (calculés automatiquement) :
 *   - time.hour          : heure courante (0-23)
 *   - time.day_of_week   : "monday", "tuesday", ...
 *   - time.is_weekend    : "true" / "false"
 *   - request.is_secure  : "true" si HTTPS
 *
 * Champs additionnels :
 *   - attributes : map pour les valeurs custom (headers, etc.)
 *
 * Exemple d'accès via path :
 *   "method"                  → "PUT"
 *   "path"                    → "/employees/42"
 *   "ip"                      → "192.168.1.50"
 *   "time.hour"               → "14"
 *   "time.day_of_week"        → "monday"
 *   "request.is_secure"       → "true"
 *   "attributes.user_agent"   → "Mozilla/5.0..."
 */
struct PolicyContext {
    std::string method;
    std::string path;
    std::string ip;
    std::chrono::system_clock::time_point now =
        std::chrono::system_clock::now();
    std::unordered_map<std::string, std::string> attributes;

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

#endif // SEA_DOMAIN_ACCESS_CONTROL_POLICY_CONTEXT_H