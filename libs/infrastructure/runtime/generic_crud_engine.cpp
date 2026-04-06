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
    // Vérification des contraintes unique
    const auto existing_records = repository_->find_all(entity_name);

    for (const auto& field : entity->fields) {
        if (!field.unique) {
            continue;
        }

        const auto incoming_it = record.find(field.name);
        if (incoming_it == record.end()) {
            continue;
        }

        const auto& incoming_value = incoming_it->second;

        for (const auto& existing : existing_records) {
            const auto existing_it = existing.find(field.name);
            if (existing_it == existing.end()) {
                continue;
            }

            if (existing_it->second == incoming_value) {
                result.errors.push_back(
                    "Valeur dupliquee pour un champ unique: " + field.name
                    );
                return result;
            }
        }
    }
    // Vérification des relations BelongsTo
    for (const auto& relation : entity->relations) {
        if (relation.kind != sea::domain::RelationKind::BelongsTo) continue;

        // Chercher la FK dans le record
        const auto fk_it = record.find(relation.fk_column);
        if (fk_it == record.end()) {
            if (relation.on_delete == sea::domain::OnDelete::Restrict) {
                result.errors.push_back("Champ FK manquant: " + relation.fk_column);
            }
            continue;
        }

        // Extraire la valeur de la FK
        std::string fk_value;
        if (std::holds_alternative<std::string>(fk_it->second))
            fk_value = std::get<std::string>(fk_it->second);
        else if (std::holds_alternative<std::int64_t>(fk_it->second))
            fk_value = std::to_string(std::get<std::int64_t>(fk_it->second));
        else {
            result.errors.push_back("FK invalide: " + relation.fk_column);
            continue;
        }

        // Vérifier que l'entité cible existe
        const auto target = repository_->find_by_id(relation.target_entity, fk_value);
        if (!target.has_value()) {
            result.errors.push_back(
                "Entite cible introuvable: " + relation.target_entity +
                " avec id=" + fk_value
                );
        }
    }

    if (!result.errors.empty()) return result;
    result.record = repository_->create(entity_name, std::move(record));
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

    result.errors = validator_->validate_partial(*entity, record);
    if (!result.errors.empty()) {
        return result;
    }
    // Vérification des relations BelongsTo
    for (const auto& relation : entity->relations) {
        if (relation.kind != sea::domain::RelationKind::BelongsTo) continue;

        // Chercher la FK dans le record
        const auto fk_it = record.find(relation.fk_column);
        if (fk_it == record.end()) {
            if (relation.on_delete == sea::domain::OnDelete::Restrict) {
                result.errors.push_back("Champ FK manquant: " + relation.fk_column);
            }
            continue;
        }

        // Extraire la valeur de la FK
        std::string fk_value;
        if (std::holds_alternative<std::string>(fk_it->second))
            fk_value = std::get<std::string>(fk_it->second);
        else if (std::holds_alternative<std::int64_t>(fk_it->second))
            fk_value = std::to_string(std::get<std::int64_t>(fk_it->second));
        else {
            result.errors.push_back("FK invalide: " + relation.fk_column);
            continue;
        }

        // Vérifier que l'entité cible existe
        const auto target = repository_->find_by_id(relation.target_entity, fk_value);
        if (!target.has_value()) {
            result.errors.push_back(
                "Entite cible introuvable: " + relation.target_entity +
                " avec id=" + fk_value
                );
        }
    }

    if (!result.errors.empty()) return result;
    sea::infrastructure::persistence::UpdateResponse updateResponse;
    updateResponse = repository_->update(entity_name, id, std::move(record));
    result.success = updateResponse.status;
    result.record = updateResponse.record;
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