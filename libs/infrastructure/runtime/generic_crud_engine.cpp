#include "generic_crud_engine.h"

namespace sea::infrastructure::runtime {

GenericCrudEngine::OperationResult
GenericCrudEngine::create(const std::string& entity_name,
                          DynamicRecord record) const {
    OperationResult result{};

    const auto* entity = registry_->find_entity(entity_name);
    if (!entity) {
        result.errors.push_back("Entite inconnue: " + entity_name);
        return result;
    }

    result.errors = validator_->validate(*entity, record);
    if (!result.errors.empty()) {
        return result;
    }

    repository_->create(entity_name, std::move(record));
    result.success = true;
    return result;
}

std::vector<DynamicRecord>
GenericCrudEngine::list(const std::string& entity_name) const {
    if (!registry_->has_entity(entity_name)) {
        return {};
    }

    return repository_->find_all(entity_name);
}

std::optional<DynamicRecord>
GenericCrudEngine::get_by_id(const std::string& entity_name,
                             const std::string& id) const {
    if (!registry_->has_entity(entity_name)) {
        return std::nullopt;
    }

    return repository_->find_by_id(entity_name, id);
}

GenericCrudEngine::OperationResult
GenericCrudEngine::update(const std::string& entity_name,
                          const std::string& id,
                          DynamicRecord record) const {
    OperationResult result{};

    const auto* entity = registry_->find_entity(entity_name);
    if (!entity) {
        result.errors.push_back("Entite inconnue: " + entity_name);
        return result;
    }

    record["id"] = id;

    result.errors = validator_->validate(*entity, record);
    if (!result.errors.empty()) {
        return result;
    }

    result.success = repository_->update(entity_name, id, std::move(record));
    if (!result.success) {
        result.errors.push_back("Impossible de mettre a jour l'enregistrement.");
    }

    return result;
}

bool GenericCrudEngine::remove(const std::string& entity_name,
                               const std::string& id) const {
    if (!registry_->has_entity(entity_name)) {
        return false;
    }

    return repository_->remove(entity_name, id);
}

} // namespace sea::infrastructure::runtime