#ifndef SEA_APPLICATION_ACCESS_CONTROL_OPERATORS_REGEX_MATCH_STRATEGY_H
#define SEA_APPLICATION_ACCESS_CONTROL_OPERATORS_REGEX_MATCH_STRATEGY_H

#include "operator_strategy.h"

#include <regex>
#include <string>
#include <unordered_map>

namespace sea::application::access_control {

/**
 * Évalue un regex_match en utilisant les regex pré-compilées au boot.
 *
 * Le regex_cache est passé au constructeur. La stratégie ne compile JAMAIS
 * de regex au runtime — c'est le PolicyCompiler qui le fait au boot.
 *
 * Si un pattern n'est pas dans le cache, l'évaluation retourne false
 * (pas d'exception au runtime, c'est une erreur silencieuse).
 */
class RegexMatchStrategy : public OperatorStrategy {
public:
    explicit RegexMatchStrategy(
        std::unordered_map<std::string, std::regex> regex_cache
        );

    bool evaluate(
        const ResolvedValue& left,
        const ResolvedValue& right
        ) const override;

    std::string_view name() const noexcept override { return "regex_match"; }

private:
    std::unordered_map<std::string, std::regex> regex_cache_;
};

} // namespace
#endif
