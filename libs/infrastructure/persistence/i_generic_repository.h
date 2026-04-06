#pragma once

#include "runtime/dynamic_record.h"

#include <optional>
#include <string>
#include <vector>

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
    virtual std::optional<runtime::DynamicRecord> create(const std::string& entity_name,
                        runtime::DynamicRecord record) = 0;

    // Retourne tous les records d’une entité
    [[nodiscard]] virtual std::vector<runtime::DynamicRecord>
    find_all(const std::string& entity_name) const = 0;

    // Retourne un record par identifiant
    [[nodiscard]] virtual std::optional<runtime::DynamicRecord>
    find_by_id(const std::string& entity_name,
               const std::string& id) const = 0;

    // Supprime un record par identifiant
    virtual bool remove(const std::string& entity_name,
                        const std::string& id) = 0;

    // Met à jour/remplace un record existant
    virtual UpdateResponse update(const std::string& entity_name,
                        const std::string& id,
                        runtime::DynamicRecord record) = 0;
};

} // namespace sea::infrastructure::persistence