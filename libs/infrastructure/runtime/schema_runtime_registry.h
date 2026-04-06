#pragma once

#include "schema.h"

#include <string>
#include <unordered_map>

namespace sea::infrastructure::runtime {

// ─────────────────────────────────────────────────────────────
// SchemaRuntimeRegistry
//
// Stocke les définitions d’entités du schéma pour le runtime.
// Permet au CRUD générique de retrouver une entité à partir
// de son nom logique.
// ─────────────────────────────────────────────────────────────
class SchemaRuntimeRegistry {
public:
    // Enregistre toutes les entités d’un schéma
    void register_schema(const sea::domain::Schema& schema);

    // Recherche une entité par son nom
    [[nodiscard]] const sea::domain::Entity* find_entity(const std::string& entity_name) const;

    // Vérifie si une entité existe
    [[nodiscard]] bool has_entity(const std::string& entity_name) const;

    // Vérifie si une entité existe avec son champ
    [[nodiscard]] const sea::domain::Field* find_field(const std::string& entity_name, const std::string& field_name) const;

    // Efface le contenu actuel
    void clear();

private:
    std::unordered_map<std::string, sea::domain::Entity> entities_;
};

} // namespace sea::infrastructure::runtime