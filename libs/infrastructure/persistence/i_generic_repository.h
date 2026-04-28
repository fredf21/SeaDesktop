#pragma once

#include "runtime/dynamic_record.h"

#include <optional>
#include <string>
#include <vector>
#include <seastar/core/future.hh>

namespace sea::infrastructure::persistence {

// ─────────────────────────────────────────────────────────────
// IGenericRepository
//
// Contrat de persistance générique pour le MVP.
// Le runtime CRUD l’utilise sans connaître l’implémentation réelle.
//
// Plus tard, il y aura plusieurs implémentations :
// - mémoire
// - PostgreSQL
// - MongoDB
// ─────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────
// Reponse du serveur lors d un update contenant la reponse et la donnEes modifiEe
// ─────────────────────────────────────────────────────────────
struct UpdateResponse {
    bool status                   = false;  // dis si le update s est bien passE
    runtime::DynamicRecord record = {};     // la donnEes modifiEe
};

class IGenericRepository {
public:
    virtual ~IGenericRepository() = default;

    // Insère ou remplace un record dans une entité logique
    virtual seastar::future<std::optional<runtime::DynamicRecord>> create(const std::string& entity_name,
                        runtime::DynamicRecord record) = 0;

    // Retourne tous les records d’une entité
    virtual seastar::future<std::vector<runtime::DynamicRecord>>
    find_all(const std::string& entity_name) = 0;

    // Retourne un record par identifiant
    virtual seastar::future<std::optional<runtime::DynamicRecord>>
    find_by_id(const std::string& entity_name,
               const std::string& id) = 0;

    virtual seastar::future<std::optional<runtime::DynamicRecord>>
    find_one_by_field(const std::string& entity_name,
                      const std::string& field_name,
                      const std::string& value) = 0;

    // Supprime un record par identifiant
    virtual seastar::future<bool> remove(const std::string& entity_name,
                        const std::string& id) = 0;

    // Met à jour/remplace un record existant
    virtual seastar::future<UpdateResponse> update(const std::string& entity_name,
                        const std::string& id,
                        runtime::DynamicRecord record) = 0;

    // Nouveau : insertion dans une table pivot many-to-many
    virtual seastar::future<bool>
    insert_pivot(const std::string& pivot_table,
                 runtime::DynamicRecord values) = 0;
};

} // namespace sea::infrastructure::persistence