#ifndef SEA_APPLICATION_ACCESS_CONTROL_OPERATORS_OPERATOR_STRATEGY_H
#define SEA_APPLICATION_ACCESS_CONTROL_OPERATORS_OPERATOR_STRATEGY_H

#include "../value_resolver.h"

namespace sea::application::access_control {

/**
 * Stratégie d'évaluation d'un opérateur.
 *
 * Chaque PolicyOperator a sa propre stratégie. Pour ajouter un nouvel
 * opérateur (ex: IpInCidr, JsonPathMatch), créer une nouvelle classe
 * qui hérite de OperatorStrategy et l'enregistrer dans OperatorRegistry.
 *
 * Convention : les stratégies sont SANS état mutable (thread-safe par défaut).
 * Si une stratégie a besoin d'un cache (ex: regex), il est passé au constructeur.
 */
class OperatorStrategy {
public:
    virtual ~OperatorStrategy() = default;

    /**
     * Évalue l'opérateur sur deux valeurs résolues.
     */
    virtual bool evaluate(
        const ResolvedValue& left,
        const ResolvedValue& right
        ) const = 0;

    /**
     * Identifiant de la stratégie (ex: "equals", "in", "regex_match").
     * Utilisé pour les logs et le debug.
     */
    virtual std::string_view name() const noexcept = 0;

    /**
     * Indique si la stratégie ignore le `right` operand.
     * Vrai pour Exists et NotExists.
     */
    virtual bool ignores_right_operand() const noexcept { return false; }
};

} // namespace sea::application::access_control

#endif