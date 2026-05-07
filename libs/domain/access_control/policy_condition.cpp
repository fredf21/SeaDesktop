#include "policy_condition.h"

#include <stdexcept>
#include <utility>

namespace sea::domain::access_control {

PolicyCondition::PolicyCondition(PolicyPredicate predicate)
    : type_(PolicyConditionType::Predicate)
    , predicate_(std::move(predicate))
{
}

PolicyCondition PolicyCondition::all_of(std::vector<PolicyCondition> children)
{
    PolicyCondition c;
    c.type_ = PolicyConditionType::All;
    c.children_ = std::move(children);
    return c;
}

PolicyCondition PolicyCondition::any_of(std::vector<PolicyCondition> children)
{
    PolicyCondition c;
    c.type_ = PolicyConditionType::Any;
    c.children_ = std::move(children);
    return c;
}

PolicyCondition PolicyCondition::not_of(PolicyCondition child)
{
    PolicyCondition c;
    c.type_ = PolicyConditionType::Not;
    c.children_.push_back(std::move(child));
    return c;
}

bool PolicyCondition::is_empty() const noexcept
{
    switch (type_) {
        case PolicyConditionType::Predicate:
            return !predicate_.has_value();
        case PolicyConditionType::All:
        case PolicyConditionType::Any:
        case PolicyConditionType::Not:
            return children_.empty();
    }
    return true;
}

void PolicyCondition::add_child(PolicyCondition child)
{
    if (type_ == PolicyConditionType::Predicate) {
        throw std::invalid_argument(
            "Cannot add child to a Predicate condition. "
            "Use type All/Any/Not for composite conditions."
        );
    }
    if (type_ == PolicyConditionType::Not && !children_.empty()) {
        throw std::invalid_argument(
            "Not condition can have only one child."
        );
    }
    children_.push_back(std::move(child));
}

void PolicyCondition::validate() const
{
    switch (type_) {
        case PolicyConditionType::Predicate:
            if (!predicate_.has_value()) {
                throw std::invalid_argument(
                    "Predicate condition must have a predicate value."
                );
            }
            if (!children_.empty()) {
                throw std::invalid_argument(
                    "Predicate condition must not have children."
                );
            }
            break;

        case PolicyConditionType::All:
        case PolicyConditionType::Any:
            if (predicate_.has_value()) {
                throw std::invalid_argument(
                    "Composite condition (All/Any) must not have a predicate."
                );
            }
            // Note : on accepte 0 enfants (sera évalué comme true pour All, false pour Any)
            for (const auto& child : children_) {
                child.validate();
            }
            break;

        case PolicyConditionType::Not:
            if (predicate_.has_value()) {
                throw std::invalid_argument(
                    "Not condition must not have a predicate."
                );
            }
            if (children_.size() != 1) {
                throw std::invalid_argument(
                    "Not condition must have exactly one child."
                );
            }
            children_[0].validate();
            break;
    }
}

} // namespace sea::domain::access_control