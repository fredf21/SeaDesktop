#ifndef SEA_APPLICATION_ACCESS_CONTROL_OPERATORS_OPERATOR_REGISTRY_H
#define SEA_APPLICATION_ACCESS_CONTROL_OPERATORS_OPERATOR_REGISTRY_H

#include "operator_strategy.h"
#include "access_control/policy_operator.h"

#include <memory>
#include <regex>
#include <string>
#include <unordered_map>

namespace sea::application::access_control {

/**
 * Catalogue des stratégies d'opérateurs.
 *
 * Construit par la factory create_default() avec toutes les stratégies
 * standard. Permet aussi d'ajouter des stratégies custom via register_strategy().
 *
 * Usage :
 *   auto registry = OperatorRegistry::create_default(regex_cache);
 *   auto* strategy = registry.find(PolicyOperator::Equals);
 *   bool result = strategy->evaluate(left, right);
 *
 * Pour étendre :
 *   registry.register_strategy(
 *     PolicyOperator::Equals,  // ou un nouveau opérateur
 *     std::make_unique<MaStrategieCustom>()
 *   );
 */
class OperatorRegistry {
public:
    OperatorRegistry() = default;

    /**
     * Crée un registre avec toutes les stratégies par défaut.
     *
     * Le regex_cache (compilé par PolicyCompiler au boot) est utilisé
     * uniquement par RegexMatchStrategy.
     */
    static OperatorRegistry create_default(
        std::unordered_map<std::string, std::regex> regex_cache = {}
        );

    /**
     * Enregistre ou remplace une stratégie pour un opérateur.
     */
    void register_strategy(
        sea::domain::access_control::PolicyOperator op,
        std::unique_ptr<OperatorStrategy> strategy
        );

    /**
     * Trouve la stratégie associée à un opérateur.
     * Retourne nullptr si non trouvée.
     */
    const OperatorStrategy* find(
        sea::domain::access_control::PolicyOperator op
        ) const;

    /**
     * Vérifie qu'une stratégie est enregistrée pour cet opérateur.
     */
    bool has(sea::domain::access_control::PolicyOperator op) const;

private:
    std::unordered_map<sea::domain::access_control::PolicyOperator,std::unique_ptr<OperatorStrategy>> strategies_;
};

} // namespace
#endif
