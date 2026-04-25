#include "generic_crud_engine.h"
#include <seastar/core/loop.hh>

namespace sea::infrastructure::runtime {
namespace {

// ─────────────────────────────────────────────
// Convertit DynamicValue -> vector<string>
// utilisé pour many-to-many
// ─────────────────────────────────────────────
std::optional<std::vector<std::string>>
extract_many_to_many_ids(const runtime::DynamicValue& value)
{
    if (std::holds_alternative<std::vector<std::string>>(value)) {
        return std::get<std::vector<std::string>>(value);
    }

    return std::nullopt;
}

// ─────────────────────────────────────────────
// Convertit DynamicValue -> string id
// ─────────────────────────────────────────────
std::optional<std::string>
dynamic_value_to_id_string(const runtime::DynamicValue& value)
{
    if (std::holds_alternative<std::string>(value)) {
        return std::get<std::string>(value);
    }

    if (std::holds_alternative<std::int64_t>(value)) {
        return std::to_string(std::get<std::int64_t>(value));
    }

    return std::nullopt;
}

} // namespace
seastar::future<GenericCrudEngine::OperationResult>
GenericCrudEngine::create(const std::string& entity_name,
                              runtime::DynamicRecord record)
{
    GenericCrudEngine::OperationResult result{};

    const auto* entity = registry_->find_entity(entity_name);
    if (!entity) {
        result.errors.push_back("Entite inconnue: " + entity_name);
        return seastar::make_ready_future<GenericCrudEngine::OperationResult>(std::move(result));
    }

    result.errors = validator_->validate(*entity, record);
    if (!result.errors.empty()) {
        return seastar::make_ready_future<GenericCrudEngine::OperationResult>(std::move(result));
    }

    return repository_->find_all(entity_name).then(
        [this, entity, entity_name, record = std::move(record), result = std::move(result)]
        (std::vector<runtime::DynamicRecord> existing_records) mutable
        -> seastar::future<GenericCrudEngine::OperationResult>
        {
            // 1. Verification des contraintes unique
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
                        return seastar::make_ready_future<GenericCrudEngine::OperationResult>(std::move(result));
                    }
                }
            }

            // 2. Verification de toutes les relations BelongsTo
            return seastar::do_for_each(
                       entity->relations,
                       [this, &record, &result](const sea::domain::Relation& relation) -> seastar::future<> {
                           if (relation.kind != sea::domain::RelationKind::BelongsTo) {
                               return seastar::make_ready_future<>();
                           }

                           const auto fk_it = record.find(relation.fk_column);
                           if (fk_it == record.end()) {
                               if (relation.on_delete == sea::domain::OnDelete::Restrict) {
                                   result.errors.push_back("Champ FK manquant: " + relation.fk_column);
                               }
                               return seastar::make_ready_future<>();
                           }

                           auto fk_value_opt = dynamic_value_to_id_string(fk_it->second);
                           if (!fk_value_opt.has_value()) {
                               result.errors.push_back("FK invalide: " + relation.fk_column);
                               return seastar::make_ready_future<>();
                           }

                           const std::string fk_value = *fk_value_opt;

                           return repository_->find_by_id(relation.target_entity, fk_value).then(
                               [&result, relation, fk_value](std::optional<runtime::DynamicRecord> target) {
                                   if (!target.has_value()) {
                                       result.errors.push_back(
                                           "Entite cible introuvable: " + relation.target_entity +
                                           " avec id=" + fk_value
                                           );
                                   }
                               }
                               );
                       }
                       ).then(
                    [this, entity, entity_name, record = std::move(record), result = std::move(result)]() mutable
                    -> seastar::future<GenericCrudEngine::OperationResult>
                    {
                        if (!result.errors.empty()) {
                            return seastar::make_ready_future<GenericCrudEngine::OperationResult>(std::move(result));
                        }

                        // 3. Creation principale
                        return repository_->create(entity_name, record).then(
                            [this, entity, record = std::move(record), result = std::move(result)]
                            (std::optional<runtime::DynamicRecord> created) mutable
                            -> seastar::future<GenericCrudEngine::OperationResult>
                            {
                                if (!created.has_value()) {
                                    result.errors.push_back("Echec lors de la creation de l'entite");
                                    return seastar::make_ready_future<GenericCrudEngine::OperationResult>(std::move(result));
                                }

                                result.record = *created;

                                // 4. Creation des liens many-to-many
                                return this->create_many_to_many_links(*entity, record, *created).then(
                                    [result = std::move(result)]
                                    (std::vector<std::string> m2m_errors) mutable
                                    -> seastar::future<GenericCrudEngine::OperationResult>
                                    {
                                        if (!m2m_errors.empty()) {
                                            result.errors.insert(
                                                result.errors.end(),
                                                std::make_move_iterator(m2m_errors.begin()),
                                                std::make_move_iterator(m2m_errors.end())
                                                );

                                            return seastar::make_ready_future<GenericCrudEngine::OperationResult>(std::move(result));
                                        }

                                        result.success = true;
                                        return seastar::make_ready_future<GenericCrudEngine::OperationResult>(std::move(result));
                                    });
                            });
                    });
        });
}
seastar::future<std::vector<DynamicRecord>>
GenericCrudEngine::list(const std::string& entity_name) const {
    if (!registry_->has_entity(entity_name)) {
        return seastar::make_ready_future<std::vector<DynamicRecord>>(std::vector<DynamicRecord>{});
    }

    return repository_->find_all(entity_name);
}

seastar::future<std::optional<DynamicRecord>>
GenericCrudEngine::get_by_id(const std::string& entity_name,
                             const std::string& id) const {
    if (!registry_->has_entity(entity_name)) {
        return seastar::make_ready_future<std::optional<DynamicRecord>>(std::nullopt);
    }

    return repository_->find_by_id(entity_name, id);
}

seastar::future<GenericCrudEngine::OperationResult>
GenericCrudEngine::update(const std::string& entity_name,
                          const std::string& id,
                          DynamicRecord record) {
    OperationResult result{};

    const auto* entity = registry_->find_entity(entity_name);
    if (!entity) {
        result.errors.push_back("Entite inconnue: " + entity_name);
        return seastar::make_ready_future<OperationResult>(result);
    }

    record["id"] = id;

    result.errors = validator_->validate_partial(*entity, record);
    if (!result.errors.empty()) {
        return seastar::make_ready_future<OperationResult>(result);
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
        return repository_->find_by_id(relation.target_entity, fk_value).then([this, result, fk_value, relation, entity_name, id, record](std::optional<runtime::DynamicRecord> target) mutable{
            if (!target.has_value()) {
                result.errors.push_back(
                    "Entite cible introuvable: " + relation.target_entity +
                    " avec id=" + fk_value
                    );
            }
            if (!result.errors.empty()) return seastar::make_ready_future<GenericCrudEngine::OperationResult>(result);
            sea::infrastructure::persistence::UpdateResponse updateResponse;
            return repository_->update(entity_name, id, std::move(record)).then([this, result](sea::infrastructure::persistence::UpdateResponse updateResponse) mutable {
                result.success = updateResponse.status;
                result.record = updateResponse.record;
                if (!result.success) {
                    result.errors.push_back("Impossible de mettre a jour l'enregistrement.");
                }

                return seastar::make_ready_future<OperationResult>(result);
            });

        });
        return seastar::make_ready_future<OperationResult>(result);

    }
    return seastar::make_ready_future<OperationResult>(result);


}

seastar::future<bool> GenericCrudEngine::remove(const std::string& entity_name,
                                                const std::string& id) {
    if (!registry_->has_entity(entity_name)) {
        return seastar::make_ready_future<bool>(false);
    }

    return repository_->remove(entity_name, id);
}
seastar::future<std::vector<std::string>>
GenericCrudEngine::create_many_to_many_links(
    const sea::domain::Entity& entity,
    const runtime::DynamicRecord& input_record,
    const runtime::DynamicRecord& created_record)
{
    std::vector<std::string> errors;

    const auto created_id_it = created_record.find("id");
    if (created_id_it == created_record.end()) {
        errors.push_back("ID manquant sur l'entite creee");
        return seastar::make_ready_future<std::vector<std::string>>(std::move(errors));
    }

    auto source_id_opt = dynamic_value_to_id_string(created_id_it->second);
    if (!source_id_opt.has_value()) {
        errors.push_back("Type d'id invalide sur l'entite creee");
        return seastar::make_ready_future<std::vector<std::string>>(std::move(errors));
    }

    const std::string source_id = *source_id_opt;

    return seastar::do_for_each(
               entity.relations,
               [this, &input_record, &errors, source_id](const sea::domain::Relation& relation) -> seastar::future<> {
                   if (relation.kind != sea::domain::RelationKind::ManyToMany) {
                       return seastar::make_ready_future<>();
                   }

                   const auto rel_it = input_record.find(relation.name);
                   if (rel_it == input_record.end()) {
                       return seastar::make_ready_future<>();
                   }

                   auto target_ids_opt = extract_many_to_many_ids(rel_it->second);
                   if (!target_ids_opt.has_value()) {
                       errors.push_back(
                           "La relation many-to-many '" + relation.name +
                           "' doit etre une liste de string"
                           );
                       return seastar::make_ready_future<>();
                   }

                   std::vector<std::string> target_ids = *target_ids_opt;

                   return seastar::do_for_each(
                       target_ids,
                       [this, &relation, &errors, source_id](const std::string& target_id) -> seastar::future<> {
                           return repository_->find_by_id(relation.target_entity, target_id).then(
                               [this, &relation, &errors, source_id, target_id]
                               (std::optional<runtime::DynamicRecord> target) -> seastar::future<> {
                                   if (!target.has_value()) {
                                       errors.push_back(
                                           "Entite cible introuvable: " + relation.target_entity +
                                           " avec id=" + target_id
                                           );
                                       return seastar::make_ready_future<>();
                                   }

                                   runtime::DynamicRecord pivot_record;
                                   pivot_record[relation.source_fk_column] = source_id;
                                   pivot_record[relation.target_fk_column] = target_id;

                                   return repository_->insert_pivot(
                                                         relation.pivot_table,
                                                         std::move(pivot_record)
                                                         ).then(
                                           [&errors, &relation, target_id](bool ok) {
                                               if (!ok) {
                                                   errors.push_back(
                                                       "Impossible de creer le lien many-to-many '" +
                                                       relation.name + "' avec id=" + target_id
                                                       );
                                               }
                                           }
                                           );
                               }
                               );
                       }
                       );
               }
               ).then([errors = std::move(errors)]() mutable {
            return seastar::make_ready_future<std::vector<std::string>>(std::move(errors));
        });
}
} // namespace sea::infrastructure::runtime