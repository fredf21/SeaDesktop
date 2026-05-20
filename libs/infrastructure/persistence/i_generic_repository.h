#pragma once

#include "runtime/dynamic_record.h"

#include <cstddef>
#include <functional>
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

// ─────────────────────────────────────────────────────────────
// Resultat d'une transaction
// ─────────────────────────────────────────────────────────────
struct TransactionResult {
    bool committed = false;  // true si COMMIT execute, false si ROLLBACK
    std::string error_message;  // message d'erreur si rollback
};

// ─────────────────────────────────────────────────────────────────────
// Pagination - types de requete et de resultat
//
// Trois modes independants, un type par mode. Chaque mode est demande
// par sa propre methode dans IGenericRepository (list_page, list_offset,
// list_cursor). Le repository ne valide PAS les bornes (limit, page) :
// c'est le role du handler HTTP, qui dispose de la PaginationConfig du
// schema. Le repository reçoit des valeurs deja normalisees.
// ─────────────────────────────────────────────────────────────────────

// Page-based : page 1-indexee + page_size.
struct PageRequest {
    std::size_t                page       = 1;   // 1-indexee (1 = premiere page)
    std::size_t                page_size  = 20;
    std::optional<std::string> sort_field;        // ex: "created_at"
    bool                       sort_desc  = false;
};

struct PageResult {
    std::vector<runtime::DynamicRecord> items;
    std::size_t                         total = 0;   // COUNT(*) global
};

// Offset/limit : offset 0-indexe + limit.
struct OffsetRequest {
    std::size_t                offset    = 0;
    std::size_t                limit     = 20;
    std::optional<std::string> sort_field;
    bool                       sort_desc = false;
};

struct OffsetResult {
    std::vector<runtime::DynamicRecord> items;
    std::size_t                         total = 0;   // COUNT(*) global
};

// Cursor : token opaque côté client (= valeur du cursor_field du dernier
// element vu). Le repository traduit en WHERE cursor_field > ? (ou <).
// Le tri est figé : impose par le schema, transmis ici tel quel.
struct CursorRequest {
    std::optional<std::string> after;          // nullopt = premiere page
    std::size_t                limit = 20;
    std::string                cursor_field;   // ex: "id"
    bool                       sort_desc = false;
};

struct CursorResult {
    std::vector<runtime::DynamicRecord> items;
    std::optional<std::string>          next_cursor;   // nullopt = derniere page
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

    // insertion dans une table pivot many-to-many
    virtual seastar::future<bool>
    insert_pivot(const std::string& pivot_table,
                 runtime::DynamicRecord values) = 0;
    // ─────────────────────────────────────────────────────────
    // delete_pivot
    //
    // Supprime une association dans une table pivot many-to-many.
    //
    // 'values' contient les colonnes identifiant l'association a
    // supprimer (ex : {source_fk: id_a, target_fk: id_b}).
    //
    // Retourne :
    //   - true si au moins une ligne a ete supprimee (association
    //     trouvee et supprimee)
    //   - false si aucune ligne ne correspondait (association
    //     inexistante)
    //
    // Symetrique d'insert_pivot.
    // ─────────────────────────────────────────────────────────
    virtual seastar::future<bool>
    delete_pivot(const std::string& pivot_table,
                 runtime::DynamicRecord values) = 0;

    // ─────────────────────────────────────────────────────────
    // pivot_exists
    //
    // Verifie si une association existe dans une table pivot
    // many-to-many.
    //
    // 'values' contient les colonnes identifiant l'association
    // recherchee (ex : {source_fk: id_a, target_fk: id_b}).
    //
    // Retourne :
    //   - true si l'association existe
    //   - false sinon
    //
    // Utilise par les handlers Attach/Detach pour distinguer :
    //   - association existante (409 sur attach, 200/204 sur detach)
    //   - association inexistante (200/204 sur attach, 404 sur detach)
    // ─────────────────────────────────────────────────────────
    virtual seastar::future<bool>
    pivot_exists(const std::string& pivot_table,
                 runtime::DynamicRecord values) = 0;

    // ─────────────────────────────────────────────────────────
    // Pagination
    //
    // Chaque mode a sa propre methode. Le handler HTTP appelle
    // celle qui correspond au mode demande dans le YAML.
    //
    // - list_page   : retourne PageResult { items, total }
    // - list_offset : retourne OffsetResult { items, total }
    // - list_cursor : retourne CursorResult { items, next_cursor }
    // - count       : utilitaire (utilise par list_page et list_offset
    //                 pour calculer 'total' ; expose au cas ou un appelant
    //                 voudrait juste compter).
    // ─────────────────────────────────────────────────────────
    virtual seastar::future<PageResult>
    list_page(const std::string& entity_name,
              const PageRequest& request) = 0;

    virtual seastar::future<OffsetResult>
    list_offset(const std::string& entity_name,
                const OffsetRequest& request) = 0;

    virtual seastar::future<CursorResult>
    list_cursor(const std::string& entity_name,
                const CursorRequest& request) = 0;

    virtual seastar::future<std::size_t>
    count(const std::string& entity_name) = 0;

    // ─────────────────────────────────────────────────────────
    // Transactions (ACID)
    //
    // Execute la lambda 'work' dans une transaction MySQL.
    //
    // Comportement :
    // - Si la lambda retourne future<true>  → COMMIT
    // - Si la lambda retourne future<false> → ROLLBACK
    // - Si la lambda lance une exception    → ROLLBACK + rethrow
    //
    // Les operations CRUD appelees DANS la lambda partagent la
    // meme connexion MySQL et donc la meme transaction.
    //
    // Exemple d'usage :
    //
    //   const auto tx_result = co_await repo->in_transaction(
    //       [&]() -> seastar::future<bool> {
    //           const auto order = co_await repo->create("Order", order_data);
    //           if (!order.has_value()) co_return false;  // rollback
    //
    //           const auto line = co_await repo->create("OrderLine", line_data);
    //           if (!line.has_value()) co_return false;   // rollback
    //
    //           co_return true;  // commit
    //       }
    //   );
    //
    // Note : les transactions ne sont pas reentrantes. Imbriquer un
    // in_transaction dans un autre n'a aucun effet (la lambda interne
    // s'execute dans la transaction externe).
    // ─────────────────────────────────────────────────────────
    virtual seastar::future<TransactionResult> in_transaction(
        std::function<seastar::future<bool>()> work
        ) = 0;
    virtual seastar::future<bool>
    increment_field(const std::string& entity_name,
                    const std::string& id,
                    const std::string& field_name,
                    std::int64_t delta) = 0;
};

} // namespace sea::infrastructure::persistence