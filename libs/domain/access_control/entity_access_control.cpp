#include "entity_access_control.h"

#include <utility>

namespace sea::domain::access_control {

void EntityAccessControl::set_spec(CrudOperation op, AccessControlSpec spec)
{
    specs_.insert_or_assign(op, std::move(spec));
}

const AccessControlSpec* EntityAccessControl::find_spec(CrudOperation op) const
{
    auto it = specs_.find(op);
    if (it == specs_.end()) {
        return nullptr;
    }
    return &it->second;
}

bool EntityAccessControl::has_spec(CrudOperation op) const
{
    return specs_.find(op) != specs_.end();
}

void EntityAccessControl::validate() const
{
    for (const auto& [op, spec] : specs_) {
        spec.validate();
    }
}

} // namespace sea::domain::access_control