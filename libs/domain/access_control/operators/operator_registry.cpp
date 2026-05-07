#include "operator_registry.h"

#include "equals_strategy.h"
#include "not_equals_strategy.h"
#include "in_strategy.h"
#include "not_in_strategy.h"
#include "contains_strategy.h"
#include "intersects_strategy.h"
#include "starts_with_strategy.h"
#include "ends_with_strategy.h"
#include "greater_than_strategy.h"
#include "greater_than_or_equal_strategy.h"
#include "less_than_strategy.h"
#include "less_than_or_equal_strategy.h"
#include "exists_strategy.h"
#include "not_exists_strategy.h"
#include "regex_match_strategy.h"

#include <utility>

namespace sea::application::access_control {

using sea::domain::access_control::PolicyOperator;

OperatorRegistry OperatorRegistry::create_default(
    std::unordered_map<std::string, std::regex> regex_cache)
{
    OperatorRegistry registry;

    registry.register_strategy(PolicyOperator::Equals,
                               std::make_unique<EqualsStrategy>());

    registry.register_strategy(PolicyOperator::NotEquals,
                               std::make_unique<NotEqualsStrategy>());

    registry.register_strategy(PolicyOperator::In,
                               std::make_unique<InStrategy>());

    registry.register_strategy(PolicyOperator::NotIn,
                               std::make_unique<NotInStrategy>());

    registry.register_strategy(PolicyOperator::Contains,
                               std::make_unique<ContainsStrategy>());

    registry.register_strategy(PolicyOperator::Intersects,
                               std::make_unique<IntersectsStrategy>());

    registry.register_strategy(PolicyOperator::StartsWith,
                               std::make_unique<StartsWithStrategy>());

    registry.register_strategy(PolicyOperator::EndsWith,
                               std::make_unique<EndsWithStrategy>());

    registry.register_strategy(PolicyOperator::GreaterThan,
                               std::make_unique<GreaterThanStrategy>());

    registry.register_strategy(PolicyOperator::GreaterThanOrEqual,
                               std::make_unique<GreaterThanOrEqualStrategy>());

    registry.register_strategy(PolicyOperator::LessThan,
                               std::make_unique<LessThanStrategy>());

    registry.register_strategy(PolicyOperator::LessThanOrEqual,
                               std::make_unique<LessThanOrEqualStrategy>());

    registry.register_strategy(PolicyOperator::Exists,
                               std::make_unique<ExistsStrategy>());

    registry.register_strategy(PolicyOperator::NotExists,
                               std::make_unique<NotExistsStrategy>());

    registry.register_strategy(PolicyOperator::RegexMatch,
                               std::make_unique<RegexMatchStrategy>(std::move(regex_cache)));

    return registry;
}

void OperatorRegistry::register_strategy(
    PolicyOperator op,
    std::unique_ptr<OperatorStrategy> strategy)
{
    strategies_[op] = std::move(strategy);
}

const OperatorStrategy* OperatorRegistry::find(PolicyOperator op) const
{
    auto it = strategies_.find(op);
    if (it == strategies_.end()) {
        return nullptr;
    }
    return it->second.get();
}

bool OperatorRegistry::has(PolicyOperator op) const
{
    return strategies_.find(op) != strategies_.end();
}

} // namespace