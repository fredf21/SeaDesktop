#include "schema_runtime_registry.h"

namespace sea::infrastructure::runtime {

void SchemaRuntimeRegistry::register_schema(const sea::domain::Schema& schema) {
    entities_.clear();

    for (const auto& entity : schema.entities) {
        entities_[entity.name] = entity;
    }
}

const sea::domain::Entity*
SchemaRuntimeRegistry::find_entity(const std::string& entity_name) const {
    const auto it = entities_.find(entity_name);
    if (it == entities_.end()) {
        return nullptr;
    }

    return &it->second;
}

bool SchemaRuntimeRegistry::has_entity(const std::string& entity_name) const {
    return find_entity(entity_name) != nullptr;
}

void SchemaRuntimeRegistry::clear() {
    entities_.clear();
}

} // namespace sea::infrastructure::runtime