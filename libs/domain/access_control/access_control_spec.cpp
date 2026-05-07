#include "access_control_spec.h"

#include "policy_value_source.h"

#include <utility>

namespace sea::domain::access_control {

AccessControlSpec::AccessControlSpec(PolicyCondition condition)
    : condition_(std::move(condition))
{
}

namespace {

// Helper récursif pour détecter si une condition utilise resource.*
bool condition_uses_resource(const PolicyCondition& cond)
{
    switch (cond.type()) {
        case PolicyConditionType::Predicate: {
            if (!cond.predicate().has_value()) {
                return false;
            }
            const auto& pred = *cond.predicate();
            return pred.left.source == PolicyValueSource::Resource
                || pred.right.source == PolicyValueSource::Resource;
        }
        case PolicyConditionType::All:
        case PolicyConditionType::Any:
        case PolicyConditionType::Not: {
            for (const auto& child : cond.children()) {
                if (condition_uses_resource(child)) {
                    return true;
                }
            }
            return false;
        }
    }
    return false;
}

} // namespace anonyme

bool AccessControlSpec::requires_resource() const noexcept
{
    return condition_uses_resource(condition_);
}

} // namespace sea::domain::access_control