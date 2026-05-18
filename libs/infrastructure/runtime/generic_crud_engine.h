#pragma once

#include "generic_validator.h"
#include "schema_runtime_registry.h"
#include "persistence/i_generic_repository.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "seastar/core/future.hh"
namespace sea::infrastructure::runtime {

// ─────────────────────────────────────────────────────────────
// GenericCrudEngine
//
// Moteur CRUD générique du MVP.
// Il utilise :
// - un registry de schéma
// - un validateur runtime
// - un repository générique
//
// Important : cette version stocke des shared_ptr,
// ce qui évite les références pendantes dans le serveur HTTP.
// ─────────────────────────────────────────────────────────────
class GenericCrudEngine {
public:
    struct OperationResult {
        bool success{false};
        std::optional<DynamicRecord> record;
        std::vector<std::string> errors;
    };

    GenericCrudEngine(
        std::shared_ptr<SchemaRuntimeRegistry> registry,
        std::shared_ptr<GenericValidator> validator,
        std::shared_ptr<sea::infrastructure::persistence::IGenericRepository> repository)
        : registry_(std::move(registry)),
        validator_(std::move(validator)),
        repository_(std::move(repository)) {
    }

    seastar::future<OperationResult> create(const std::string& entity_name,
                                            DynamicRecord record);

    seastar::future<std::vector<DynamicRecord>>
    list(const std::string& entity_name) const;

    seastar::future<std::optional<DynamicRecord>>
    get_by_id(const std::string& entity_name,
              const std::string& id) const;

    seastar::future<std::optional<DynamicRecord>>
    find_one_by_field(const std::string& entity_name,
                      const std::string& field_name,
                      const std::string& value) const;

    seastar::future<OperationResult> update(const std::string& entity_name,
                                            const std::string& id,
                                            DynamicRecord record);

    seastar::future<bool> remove(const std::string& entity_name,
                                 const std::string& id);
    // ── Pagination ──────────────────────────────────────────
    //
    // Trois modes independants. Le handler HTTP appelle la methode
    // correspondant au mode demande dans le YAML, apres avoir parse
    // et valide les query params via pagination_query::parse_*_query.
    //
    // Note : aucune validation supplementaire ici. Le repository
    // est appele tel quel avec la request normalisee.

    seastar::future<sea::infrastructure::persistence::PageResult>
    list_page(const std::string& entity_name,
              const sea::infrastructure::persistence::PageRequest& request) const;

    seastar::future<sea::infrastructure::persistence::OffsetResult>
    list_offset(const std::string& entity_name,
                const sea::infrastructure::persistence::OffsetRequest& request) const;

    seastar::future<sea::infrastructure::persistence::CursorResult>
    list_cursor(const std::string& entity_name,
                const sea::infrastructure::persistence::CursorRequest& request) const;

    // ─────────────────────────────────────────────────────────
    // get_repository
    //
    // Accès direct au repository sous-jacent. Utilisé par les
    // handlers qui ont besoin d'orchestrer une transaction SQL
    // englobant plusieurs opérations (ex: handler avec champs File
    // qui doit faire `extract → create → retain` atomiquement).
    //
    // Note de design : oui, ça casse partiellement l'abstraction
    // "le CrudEngine est la seule porte d'entrée". Mais l'alternative
    // (ajouter des méthodes create_with_files, update_with_files, etc.
    // au CrudEngine) coupleraient l'engine au domaine File, ce qui est
    // pire. Le getter est une concession ciblée et documentée.
    // ─────────────────────────────────────────────────────────
    [[nodiscard]] std::shared_ptr<sea::infrastructure::persistence::IGenericRepository>
    get_repository() const noexcept { return repository_; }


private:
    std::shared_ptr<SchemaRuntimeRegistry> registry_;
    std::shared_ptr<GenericValidator> validator_;
    std::shared_ptr<sea::infrastructure::persistence::IGenericRepository> repository_;
    seastar::future<std::vector<std::string>>
    create_many_to_many_links(const sea::domain::Entity& entity,
                              const runtime::DynamicRecord& input_record,
                              const runtime::DynamicRecord& created_record);
};

} // namespace sea::infrastructure::runtime