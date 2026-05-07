#ifndef SEA_DOMAIN_ACCESS_CONTROL_ABAC_MODE_H
#define SEA_DOMAIN_ACCESS_CONTROL_ABAC_MODE_H

#include <optional>
#include <string>
#include <string_view>

namespace sea::domain::access_control {

/**
 * Mode d'evaluation des regles ABAC (Attribute-Based Access Control)
 * resource-aware au niveau du middleware.
 *
 * Une regle est dite "resource-aware" quand elle reference resource.X
 * (ex: own_resource: true, same_scope: true). Ces regles ne peuvent
 * pas etre evaluees au middleware car la ressource n'a pas encore
 * ete chargee depuis la DB.
 *
 * Permissive (defaut) :
 *   - Le middleware evalue uniquement la partie subject-only (fast path).
 *   - Si la partie subject-only passe, la requete est laissee passer
 *     au handler. Le handler / Module 6 chargera la ressource et
 *     finalisera l'evaluation (post-handler ABAC check).
 *   - Les regles ABAC fonctionnent normalement.
 *
 * Strict :
 *   - Le middleware refuse 403 immediatement toute regle qui contient
 *     une partie resource-aware.
 *   - Aucune route utilisant own_resource ou same_scope ne sera
 *     accessible.
 *   - Utile pour des scenarios de bypass temporaire ou d'audit
 *     extreme. NE PAS utiliser en production normale.
 */
enum class AbacMode {
    Permissive,
    Strict
};

/**
 * Convertit une string ("permissive" / "strict") en AbacMode.
 * Retourne nullopt si la string n'est pas reconnue.
 */
[[nodiscard]] std::optional<AbacMode> abac_mode_from_string(
    const std::string& s) noexcept;

/**
 * Convertit un AbacMode en string ("permissive" / "strict").
 */
[[nodiscard]] std::string_view to_string(AbacMode mode) noexcept;

} // namespace sea::domain::access_control

#endif // SEA_DOMAIN_ACCESS_CONTROL_ABAC_MODE_H