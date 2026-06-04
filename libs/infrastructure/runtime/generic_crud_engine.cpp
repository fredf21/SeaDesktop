#include "generic_crud_engine.h"
#include "spdlog/spdlog.h"
#include <seastar/core/do_with.hh>
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
// ─────────────────────────────────────────────────────────────────
// GenericCrudEngine::create
//
// Reecrit avec seastar::do_with (juin 2026) pour corriger un bug
// de durée de vie : l'ancienne implementation chainait des lambdas
// imbriquees ou chacune capturait `result = std::move(result)`.
// L'evaluation des arguments du `.then()` C++ se faisant AVANT
// l'execution du `do_for_each` qui les precede, le `std::move(result)`
// etait execute trop tot. La boucle BelongsTo ecrivait dans un
// `result` zombi (moved-from), et la lambda suivante lisait son
// propre `result` (la copie deplacee) qui n'avait jamais vu les
// push_back.
//
// Symptome observe : POST /projects avec team_id invalide renvoyait
// "Failed to create entity" (du repo, contrainte FK MySQL) au lieu
// de "Target entity not found: Team with id=..." (validation
// applicative).
//
// Fix : seastar::do_with donne a `record` et `result` une duree de
// vie explicite qui survit a toute la chaine de futures. Toutes les
// lambdas internes capturent &record, &result par reference, sans
// move.
// ─────────────────────────────────────────────────────────────────
seastar::future<GenericCrudEngine::OperationResult>
GenericCrudEngine::create(const std::string& entity_name,
                          runtime::DynamicRecord record)
{
    // Validations synchrones qui peuvent court-circuiter sans
    // entrer dans la chaine de futures (gain de perf et de
    // lisibilite : si on echoue ici, on sort tout de suite).
    GenericCrudEngine::OperationResult result{};

    const auto* entity = registry_->find_entity(entity_name);
    if (!entity) {
        result.errors.push_back("Unknown entity: " + entity_name);
        return seastar::make_ready_future<GenericCrudEngine::OperationResult>(
            std::move(result));
    }

    result.errors = validator_->validate(*entity, record);
    if (!result.errors.empty()) {
        return seastar::make_ready_future<GenericCrudEngine::OperationResult>(
            std::move(result));
    }

    // ─── seastar::do_with : ownership explicite de record et result ─
    // Toutes les lambdas qui suivent capturent &record / &result
    // par reference. do_with garantit que ces variables vivent
    // pendant toute la chaine, peu importe quand chaque future est
    // resolue. C'est le pattern Seastar idiomatique pour ce cas.
    return seastar::do_with(
        std::move(record),
        std::move(result),
        [this, entity, entity_name]
        (runtime::DynamicRecord& record,
         GenericCrudEngine::OperationResult& result)
        -> seastar::future<GenericCrudEngine::OperationResult>
        {
            // ── 1. Verification des contraintes unique ───────────
            // Necessaire de lire tous les records existants
            // (`find_all`) pour comparer champ par champ.
            return repository_->find_all(entity_name).then(
                [this, entity, entity_name, &record, &result]
                (std::vector<runtime::DynamicRecord> existing_records)
                -> seastar::future<GenericCrudEngine::OperationResult>
                {
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
                                    "Duplicate value for unique field: " + field.name
                                    );
                                return seastar::make_ready_future<
                                    GenericCrudEngine::OperationResult>(result);
                            }
                        }
                    }

                    // ── 2. Verification des relations BelongsTo ─
                    // Pour chaque relation, on verifie que la FK
                    // pointe vers un record existant. Les erreurs
                    // s'accumulent dans result.errors (acces par
                    // reference, garanti vivant par do_with).
                    return seastar::do_for_each(
                               entity->relations,
                               [this, &record, &result]
                               (const sea::domain::Relation& relation)
                               -> seastar::future<>
                               {
                                   if (relation.kind != sea::domain::RelationKind::BelongsTo) {
                                       return seastar::make_ready_future<>();
                                   }

                                   const auto fk_it = record.find(relation.fk_column);
                                   if (fk_it == record.end()) {
                                       // FK requise (Restrict) absente du record :
                                       // erreur explicite. Sinon (Cascade/SetNull),
                                       // FK optionnelle = skip silencieux.
                                       if (relation.on_delete == sea::domain::OnDelete::Restrict) {
                                           result.errors.push_back(
                                               "Missing FK field: " + relation.fk_column);
                                       }
                                       return seastar::make_ready_future<>();
                                   }

                                   auto fk_value_opt = dynamic_value_to_id_string(
                                       fk_it->second);
                                   if (!fk_value_opt.has_value()) {
                                       result.errors.push_back(
                                           "Invalid FK: " + relation.fk_column);
                                       return seastar::make_ready_future<>();
                                   }

                                   const std::string fk_value = *fk_value_opt;

                                   return repository_->find_by_id(
                                                         relation.target_entity, fk_value).then(
                                           [&result, relation, fk_value]
                                           (std::optional<runtime::DynamicRecord> target)
                                           {
                                               if (!target.has_value()) {
                                                   result.errors.push_back(
                                                       "Target entity not found: "
                                                       + relation.target_entity
                                                       + " with id=" + fk_value);
                                               }
                                           });
                               }
                               ).then(
                            [this, entity, entity_name, &record, &result]()
                            -> seastar::future<GenericCrudEngine::OperationResult>
                            {
                                // Si erreurs accumulees (unique deja gere
                                // plus haut, ou BelongsTo introuvable),
                                // on abort sans toucher a la base.
                                if (!result.errors.empty()) {
                                    return seastar::make_ready_future<
                                        GenericCrudEngine::OperationResult>(result);
                                }

                                // ── 3. Creation principale ─────────
                                // On passe une copie de record au repo
                                // (pas un move) car on en a encore besoin
                                // pour create_many_to_many_links ensuite.
                                return repository_->create(entity_name, record).then(
                                    [this, entity, &record, &result]
                                    (std::optional<runtime::DynamicRecord> created)
                                    -> seastar::future<GenericCrudEngine::OperationResult>
                                    {
                                        if (!created.has_value()) {
                                            result.errors.push_back(
                                                "Failed to create entity");
                                            return seastar::make_ready_future<
                                                GenericCrudEngine::OperationResult>(
                                                result);
                                        }

                                        result.record = *created;

                                        // ── 4. Liens many-to-many ──
                                        // Si l'entite a des relations
                                        // ManyToMany, on cree les lignes
                                        // pivot correspondantes.
                                        return this->create_many_to_many_links(
                                                       *entity, record, *created).then(
                                                [&result]
                                                (std::vector<std::string> m2m_errors)
                                                -> seastar::future<
                                                    GenericCrudEngine::OperationResult>
                                                {
                                                    if (!m2m_errors.empty()) {
                                                        result.errors.insert(
                                                            result.errors.end(),
                                                            std::make_move_iterator(
                                                                m2m_errors.begin()),
                                                            std::make_move_iterator(
                                                                m2m_errors.end())
                                                            );
                                                        return seastar::make_ready_future<
                                                            GenericCrudEngine::OperationResult>(
                                                            result);
                                                    }

                                                    result.success = true;
                                                    return seastar::make_ready_future<
                                                        GenericCrudEngine::OperationResult>(
                                                        result);
                                                });
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

seastar::future<std::optional<DynamicRecord>>
GenericCrudEngine::find_one_by_field(const std::string& entity_name,
                                     const std::string& field_name,
                                     const std::string& value) const
{
    if (!registry_->has_entity(entity_name)) {
        return seastar::make_ready_future<std::optional<DynamicRecord>>(std::nullopt);
    }

    return repository_->find_one_by_field(entity_name, field_name, value);
}


// ─────────────────────────────────────────────────────────────────
// GenericCrudEngine::update
//
// Reecrit avec seastar::do_with (juin 2026, lendemain du fix bug 11
// dans create) pour corriger un bug 11 LATENT decouvert par le
// harnais e2e :
//
// L'ancienne implementation (fix bug 9 du 1er juin) avait le meme
// pattern probleme que create() avait avant son propre fix :
//   - result vivait dans le scope de la fonction
//   - le .then() qui suivait do_for_each capturait `result = std::move(result)`
//   - les lambdas de la boucle BelongsTo capturaient &result
//   - selon l'ordre d'evaluation, &result pouvait pointer sur un
//     result moved-from quand find_by_id().then() s'executait
//
// Le bug est reste invisible pendant un jour parce qu'aucun test ne
// declenchait simultanement :
//   - une relation BelongsTo presente dans le record
//   - une cible inexistante (faisant entrer dans la branche
//     push_back("Target entity not found..."))
//
// Le test test_update_project_avec_fk_invalide_echoue couvre
// exactement ce cas. Et son ajout en parallele du fix bug 12
// (qui modifiait la taille d'OperationResult, changeant la
// disposition memoire) a transforme la corruption silencieuse
// en stack smashing detectable.
//
// Fix identique a create() : seastar::do_with pour ownership
// explicite. Aucun std::move dans les captures.
// ─────────────────────────────────────────────────────────────────
seastar::future<GenericCrudEngine::OperationResult>
GenericCrudEngine::update(const std::string& entity_name,
                          const std::string& id,
                          DynamicRecord record)
{
    // Validations synchrones qui court-circuitent sans entrer dans
    // la chaine de futures.
    OperationResult result{};

    const auto* entity = registry_->find_entity(entity_name);
    if (!entity) {
        result.errors.push_back("Unknown entity: " + entity_name);
        return seastar::make_ready_future<OperationResult>(std::move(result));
    }

    record["id"] = id;

    result.errors = validator_->validate_partial(*entity, record);
    if (!result.errors.empty()) {
        return seastar::make_ready_future<OperationResult>(std::move(result));
    }

    // ─── seastar::do_with : ownership explicite de record et result ─
    // Toutes les lambdas qui suivent capturent &record / &result
    // par reference. do_with garantit que ces variables vivent
    // pendant toute la chaine, peu importe quand chaque future est
    // resolue. Pattern Seastar idiomatique pour ce cas (cf. create()).
    return seastar::do_with(
        std::move(record),
        std::move(result),
        [this, entity, entity_name, id]
        (runtime::DynamicRecord& record,
         GenericCrudEngine::OperationResult& result)
        -> seastar::future<GenericCrudEngine::OperationResult>
        {
            // ─── 1. Verification de TOUTES les relations BelongsTo ──
            // Pour chaque relation BelongsTo dont la FK est presente
            // dans le record (i.e. touchee par l'update), on verifie
            // que la cible existe. Les erreurs s'accumulent dans
            // result.errors. Si la FK n'est PAS touchee par l'update,
            // on skip silencieusement (validation 'required' faite
            // au create).
            return seastar::do_for_each(
                       entity->relations,
                       [this, &record, &result]
                       (const sea::domain::Relation& relation) -> seastar::future<>
                       {
                           if (relation.kind != sea::domain::RelationKind::BelongsTo) {
                               return seastar::make_ready_future<>();
                           }

                           const auto fk_it = record.find(relation.fk_column);
                           if (fk_it == record.end()) {
                               // FK absente du record d'update = champ non
                               // touche par l'update partiel. Legitime.
                               return seastar::make_ready_future<>();
                           }

                           auto fk_value_opt = dynamic_value_to_id_string(
                               fk_it->second);
                           if (!fk_value_opt.has_value()) {
                               result.errors.push_back(
                                   "Invalid FK: " + relation.fk_column);
                               return seastar::make_ready_future<>();
                           }

                           const std::string fk_value = *fk_value_opt;

                           return repository_->find_by_id(
                                                 relation.target_entity, fk_value).then(
                                   [&result, relation, fk_value]
                                   (std::optional<runtime::DynamicRecord> target)
                                   {
                                       if (!target.has_value()) {
                                           result.errors.push_back(
                                               "Target entity not found: "
                                               + relation.target_entity
                                               + " with id=" + fk_value);
                                       }
                                   });
                       }
                       ).then(
                    [this, entity_name, id, &record, &result]()
                    -> seastar::future<GenericCrudEngine::OperationResult>
                    {
                        // Si erreurs accumulees (FK invalide, cible
                        // introuvable), on abort sans toucher a la base.
                        if (!result.errors.empty()) {
                            return seastar::make_ready_future<
                                GenericCrudEngine::OperationResult>(result);
                        }

                        // ─── 2. UPDATE SQL — appele dans tous les cas ─
                        // Y compris pour les entites SANS relation
                        // BelongsTo (Document, User si pas de FK, etc.).
                        return repository_->update(
                                              entity_name, id, std::move(record)).then(
                                [&result]
                                (sea::infrastructure::persistence::UpdateResponse resp)
                                -> seastar::future<GenericCrudEngine::OperationResult>
                                {
                                    result.success = resp.status;
                                    result.record = resp.record;
                                    if (!result.success) {
                                        result.errors.push_back(
                                            "Unable to update record.");
                                    }
                                    return seastar::make_ready_future<
                                        GenericCrudEngine::OperationResult>(result);
                                });
                    });
        });
}
seastar::future<bool>
GenericCrudEngine::pivot_exists(
    const std::string& pivot_table,
    runtime::DynamicRecord values)
{
    return repository_->pivot_exists(pivot_table, std::move(values));
}

seastar::future<bool>
GenericCrudEngine::delete_pivot(
    const std::string& pivot_table,
    runtime::DynamicRecord values)
{
    return repository_->delete_pivot(pivot_table, std::move(values));
}

seastar::future<GenericCrudEngine::OperationResult>
GenericCrudEngine::remove(const std::string& entity_name,
                          const std::string& id)
{
    OperationResult result{};

    if (!registry_->has_entity(entity_name)) {
        result.errors.push_back("Unknown entity: " + entity_name);
        return seastar::make_ready_future<OperationResult>(std::move(result));
    }

    // ─── 1. Verification on_delete=restrict (relations entrantes) ──
    // On scanne toutes les entités du registry pour trouver celles
    // dont une relation BelongsTo cible 'entity_name' avec
    // on_delete=Restrict. Pour chacune, on vérifie via find_one_by_field
    // qu'aucun record n'y pointe.
    //
    // C'est l'enforcement APPLICATIF du restrict, indépendant des
    // contraintes FK MySQL. Avantages :
    //   - portable (Postgres, Mongo, etc.)
    //   - message d'erreur précis (409 Conflict + nom de l'entité
    //     bloquante), au lieu de l'erreur générique du repo qui
    //     ressemblait à 'Enregistrement introuvable' (404 trompeur)
    //   - cohérent avec la déclaration on_delete: restrict du YAML
    //
    // Note : on collecte d'abord la liste des checks à faire, puis on
    // les exécute via do_for_each. Si UNE référence est trouvée, on
    // enregistre l'erreur et on continue le scan (pour donner au client
    // une vue exhaustive des blocages s'il y en a plusieurs).

    struct IncomingRefCheck {
        std::string source_entity;
        std::string fk_column;
    };

    std::vector<IncomingRefCheck> checks;
    for (const auto& [name, entity] : registry_->all_entities()) {
        for (const auto& relation : entity.relations) {
            if (relation.kind == sea::domain::RelationKind::BelongsTo
                && relation.target_entity == entity_name
                && relation.on_delete == sea::domain::OnDelete::Restrict) {
                checks.push_back({name, relation.fk_column});
            }
        }
    }
    return seastar::do_with(
        std::move(result),
        std::move(checks),
        [this, entity_name, id](auto& result, auto& checks)
        -> seastar::future<OperationResult>
        {
            return seastar::do_for_each(
                       checks,
                       [this, id, &result](const IncomingRefCheck& check)
                       -> seastar::future<>
                       {
                           return repository_->find_one_by_field(
                                                 check.source_entity, check.fk_column, id
                                                 ).then([&result, check,  id](std::optional<runtime::DynamicRecord> ref) {
                                   if (ref.has_value()) {
                                       result.restrict_violation = true;
                                       result.errors.push_back(
                                           "Cannot delete: referenced by " + check.source_entity
                                           + " via " + check.fk_column
                                           + " (on_delete=restrict)"
                                           );
                                   }
                               });
                       }
                       ).then([this, entity_name, id, &result]()
                      -> seastar::future<OperationResult>
                      {
                          // Si des références restrict bloquent → 409 logique
                          if (!result.errors.empty()) {
                              // result.success reste false (init)
                              return seastar::make_ready_future<OperationResult>(result);
                          }

                          // ── 2. DELETE effectif via le repo ───────────────
                          return repository_->remove(entity_name, id).then(
                              [&result](bool deleted) {
                                  result.success = deleted;
                                  if (!deleted) {
                                      result.errors.push_back("Record not found.");
                                  }
                                  return seastar::make_ready_future<OperationResult>(result);
                              });
                      });
        });
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
        errors.push_back("Missing ID on created entity");
        return seastar::make_ready_future<std::vector<std::string>>(std::move(errors));
    }

    auto source_id_opt = dynamic_value_to_id_string(created_id_it->second);
    if (!source_id_opt.has_value()) {
        errors.push_back("Invalid ID type on created entity");
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
                           "The many-to-many relation '" + relation.name +
                           "' must be a list of strings"
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
                                           "Target entity not found: " + relation.target_entity +
                                           " with id=" + target_id
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
                                                       "Unable to create many-to-many link '" +
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

seastar::future<sea::infrastructure::persistence::PageResult>
GenericCrudEngine::list_page(
    const std::string& entity_name,
    const sea::infrastructure::persistence::PageRequest& request) const
{
    return repository_->list_page(entity_name, request);
}

seastar::future<sea::infrastructure::persistence::OffsetResult>
GenericCrudEngine::list_offset(
    const std::string& entity_name,
    const sea::infrastructure::persistence::OffsetRequest& request) const
{
    return repository_->list_offset(entity_name, request);
}

seastar::future<sea::infrastructure::persistence::CursorResult>
GenericCrudEngine::list_cursor(
    const std::string& entity_name,
    const sea::infrastructure::persistence::CursorRequest& request) const
{
    return repository_->list_cursor(entity_name, request);
}
// ═══════════════════════════════════════════════════════════════════
// C'est tout. Le passthrough est volontairement minimaliste :
//
// - Pas de validation (deja faite cote handler via pagination_query)
// - Pas de transformation (le repo retourne directement le bon type)
// - Pas de count() ici (on l'a deja dans le repo et il est appele
//   implicitement par list_page/list_offset pour calculer 'total')
//
// Si demain on veut faire du caching ou de l'audit log sur les listings
// pagines, c'est ici qu'on l'ajoutera.
// ═══════════════════════════════════════════════════════════════════

} // namespace sea::infrastructure::runtime