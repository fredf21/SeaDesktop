#pragma once

#include "../i_generic_repository.h"

#include <unordered_map>
#include <string>
#include <optional>

namespace sea::infrastructure::persistence {

/**
 * InMemoryGenericRepository
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

    seastar::future<sea::infrastructure::persistence::TransactionResult>
    in_transaction(
        std::function<seastar::future<bool>()> work
        ) override;

    // ── Pagination ──────────────────────────────────────────
    //
    // Les 3 modes sont supportes mais l'implementation reste naive :
    // on materialise toutes les lignes puis on trie/coupe en memoire.
    // C'est volontaire — ce backend sert aux tests et au dev.

    seastar::future<PageResult>
    list_page(const std::string& entity_name,
              const PageRequest& request) override;

    seastar::future<OffsetResult>
    list_offset(const std::string& entity_name,
                const OffsetRequest& request) override;

    seastar::future<CursorResult>
    list_cursor(const std::string& entity_name,
                const CursorRequest& request) override;

    seastar::future<std::size_t>
    count(const std::string& entity_name) override;
    seastar::future<bool> delete_pivot(const std::string &pivot_table, runtime::DynamicRecord values) override;
    seastar::future<bool> pivot_exists(const std::string &pivot_table, runtime::DynamicRecord values) override;
    // increment_field : atomique en mode mono-shard (Seastar shared-nothing
    // garantit qu'un shard est mono-thread). Cf. IGenericRepository pour la
    // doc complète.
    seastar::future<bool>
    increment_field(const std::string& entity_name,
                    const std::string& id,
                    const std::string& field_name,
                    std::int64_t delta) override;
    seastar::future<bool>
    decrement_field_if_positive(const std::string &entity_name,
                                const std::string &id,
                                const std::string &field_name) override;
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
     */
    [[nodiscard]] std::optional<std::string>
    extract_id(const runtime::DynamicRecord& record) const;

    // Helper interne pagination : récupère tous les records triés
    // selon (sort_field, sort_desc). Si sort_field est nullopt, ordre
    // d'itération (non garanti stable mais déterministe par appel).
    [[nodiscard]] std::vector<runtime::DynamicRecord>
    collect_all_sorted(const std::string& entity_name,
                       const std::optional<std::string>& sort_field,
                       bool sort_desc) const;


};

} // namespace sea::infrastructure::persistence