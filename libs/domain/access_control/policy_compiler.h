#ifndef SEA_APPLICATION_ACCESS_CONTROL_POLICY_COMPILER_H
#define SEA_APPLICATION_ACCESS_CONTROL_POLICY_COMPILER_H

#include "access_control/policy_condition.h"

#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

namespace sea::application::access_control {

/**
 * Pré-compile et valide les règles d'autorisation au boot du serveur.
 *
 * Responsabilités :
 *   1. Parcourir tous les PolicyCondition pour trouver les RegexMatch
 *   2. Compiler les patterns regex (ce qui valide la syntaxe)
 *   3. Stocker dans une map (pattern_string → std::regex compilé)
 *   4. Valider qu'aucune condition n'est structurellement invalide
 *
 * Le résultat est passé au PolicyEngine au runtime.
 *
 * Si une regex est invalide, throw au boot → fail-fast, pas de surprise au runtime.
 */
class PolicyCompiler {
public:
    /**
     * Compile une liste de conditions et retourne le cache regex.
     *
     * À appeler UNE SEULE FOIS au boot avec toutes les conditions
     * déclarées dans le YAML.
     *
     * Throw std::runtime_error si :
     *   - Une regex est syntaxiquement invalide
     *   - Une condition est structurellement invalide
     */
    static std::unordered_map<std::string, std::regex> compile_all(
        const std::vector<sea::domain::access_control::PolicyCondition>& conditions
        );

    /**
     * Compile une seule condition (pratique pour les tests).
     */
    static std::unordered_map<std::string, std::regex> compile_one(
        const sea::domain::access_control::PolicyCondition& condition);

private:
    /**
     * Parcours récursif d'une condition pour extraire les patterns regex.
     */
    static void collect_regex_patterns(
        const sea::domain::access_control::PolicyCondition& condition,
        std::vector<std::string>& patterns);
};

} // namespace sea::application::access_control

#endif // SEA_APPLICATION_ACCESS_CONTROL_POLICY_COMPILER_H