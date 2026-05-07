#include "policy_compiler.h"

#include <stdexcept>

namespace sea::application::access_control {

using namespace sea::domain::access_control;

std::unordered_map<std::string, std::regex> PolicyCompiler::compile_all(
    const std::vector<PolicyCondition>& conditions)
{
    // 1. Valider chaque condition structurellement
    for (const auto& condition : conditions) {
        condition.validate();
    }

    // 2. Collecter tous les patterns regex
    std::vector<std::string> patterns;
    for (const auto& condition : conditions) {
        collect_regex_patterns(condition, patterns);
    }

    // 3. Compiler chaque pattern (dédupliqué)
    std::unordered_map<std::string, std::regex> cache;
    for (const auto& pattern : patterns) {
        if (cache.count(pattern)) continue;  // déjà compilé

        try {
            cache.emplace(pattern, std::regex(pattern));
        } catch (const std::regex_error& e) {
            throw std::runtime_error(
                "PolicyCompiler: invalid regex pattern '" + pattern +
                "': " + e.what()
                );
        }
    }

    return cache;
}

std::unordered_map<std::string, std::regex> PolicyCompiler::compile_one(
    const PolicyCondition& condition)
{
    return compile_all({condition});
}

void PolicyCompiler::collect_regex_patterns(
    const PolicyCondition& condition,
    std::vector<std::string>& patterns)
{
    switch (condition.type()) {
    case PolicyConditionType::Predicate: {
        if (!condition.predicate().has_value()) return;
        const auto& pred = *condition.predicate();

        if (pred.op == PolicyOperator::RegexMatch) {
            // Le pattern doit être dans right.literal
            if (pred.right.source == PolicyValueSource::Literal
                && !pred.right.literal.empty()) {
                patterns.push_back(pred.right.literal);
            }
        }
        return;
    }
    case PolicyConditionType::All:
    case PolicyConditionType::Any:
    case PolicyConditionType::Not: {
        for (const auto& child : condition.children()) {
            collect_regex_patterns(child, patterns);
        }
        return;
    }
    }
}

} // namespace sea::application::access_control