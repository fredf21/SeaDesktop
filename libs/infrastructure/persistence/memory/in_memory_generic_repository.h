#pragma once

#include "../i_generic_repository.h"

#include <unordered_map>
#include <string>
#include <optional>

namespace sea::infrastructure::persistence {

/**
 * 🧠 InMemoryGenericRepository
 *
 * Implémentation simple en mémoire (RAM).
 *
 * Caractéristiques :
 * - Aucune persistance disque
 * - Aucune opération bloquante
 * - Thread unsafe (OK en Seastar si utilisé par shard)
 *
 * Usage :
 * - Tests
 * - MVP
 * - fallback sans base de données
 */
class InMemoryGenericRepository final : public IGenericRepository {
public:

    /**
     * Crée un nouvel enregistrement.
     *
     * @param entity_name Nom de l'entité (ex: "User")
     * @param record Données dynamiques (clé-valeur)
     */
    seastar::future<std::optional<runtime::DynamicRecord>>
    create(const std::string& entity_name,
           runtime::DynamicRecord record) override;

    /**
     * Retourne tous les enregistrements d'une entité.
     */
    seastar::future<std::vector<runtime::DynamicRecord>>
    find_all(const std::string& entity_name) override;

    /**
     * Recherche un enregistrement par ID.
     */
    seastar::future<std::optional<runtime::DynamicRecord>>
    find_by_id(const std::string& entity_name,
               const std::string& id) override;

    /**
     * Supprime un enregistrement par ID.
     */
    seastar::future<bool>
    remove(const std::string& entity_name,
           const std::string& id) override;

    /**
     * Met à jour un enregistrement existant.
     */
    seastar::future<UpdateResponse>
    update(const std::string& entity_name,
           const std::string& id,
           runtime::DynamicRecord record) override;

    /**
     * Recherche un enregistrement par un champ spécifique.
     *
     * Exemple :
     * find_one_by_field("User", "email", "test@mail.com")
     */
    seastar::future<std::optional<runtime::DynamicRecord>>
    find_one_by_field(const std::string& entity_name,
                      const std::string& field_name,
                      const std::string& value) override;

    /**
     * Insère dans une table pivot (relation many-to-many).
     *
     * Ici : stockage simplifié en mémoire.
     */
    seastar::future<bool>
    insert_pivot(const std::string& pivot_table,
                 runtime::DynamicRecord values) override;

private:

    /**
     * Structure interne :
     *
     * entity_name -> (id -> record)
     *
     * Exemple :
     * "User" -> {
     *    "1" -> {...},
     *    "2" -> {...}
     * }
     */
    using EntityStorage =
        std::unordered_map<std::string, runtime::DynamicRecord>;

    std::unordered_map<std::string, EntityStorage> storage_;

    /**
     * Extrait l'ID depuis un record.
     *
     * Convention :
     * - le champ "id" doit exister
     * - doit être convertible en string
     */
    [[nodiscard]]
    std::optional<std::string>
    extract_id(const runtime::DynamicRecord& record) const;
};

} // namespace sea::infrastructure::persistence