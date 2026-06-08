#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "route_generator.h"
#include "schema.h"
#include "service.h"

namespace sea::application {

class OpenApiGenerator {
public:
    using json = nlohmann::json;

    OpenApiGenerator();

    json generate(
        const domain::Service& service,
        const std::vector<RouteDefinition>& route_definitions
        ) const;

private:
    // Helpers de sécurité
    bool service_has_auth(const domain::Service& service) const;
    bool schema_has_auth_source(const domain::Schema& schema) const;

    // Génération de schémas
    json make_entity_schema(const domain::Entity& entity) const;
    json make_entity_input_schema(const domain::Entity& entity) const;
    json field_to_openapi_schema(const domain::Field& field) const;
    void add_auth_schemas(json& schemas) const;

    // Génération de paths
    void add_crud_path(
        json& paths,
        const RouteDefinition& route,
        const domain::Service& service
        ) const;

    void add_relation_paths(
        json& paths,
        const domain::Service& service
        ) const;

    void add_auth_paths(json& paths) const;
    void add_health_path(json& paths) const;
    // ─────────────────────────────────────────────────────────────
    // Pagination
    // ─────────────────────────────────────────────────────────────

    // Ajoute dans 'paths' tous les endpoints pagines (_page, _offset,
    // _cursor) trouves dans route_definitions.
    //
    // Particularite : les routes paginees sont detectees par le
    // suffixe d'operation_name, pas par leur path.
    void add_pagination_paths(
        json& paths,
        const std::vector<RouteDefinition>& route_definitions,
        const domain::Service& service
        ) const;

    // Schemas d'enveloppes JSON pour les 3 modes de pagination.
    // Inclus dans components/schemas comme <Entity>PageEnvelope etc.
    // Les "items" pointent via $ref vers le schema de l'entite.

    // { items: [<Entity>], page, page_size, total, total_pages, sort? }
    json make_page_envelope_schema(const domain::Entity& entity) const;

    // { items: [<Entity>], offset, limit, total, sort? }
    json make_offset_envelope_schema(const domain::Entity& entity) const;

    // { items: [<Entity>], limit, next_cursor? }
    json make_cursor_envelope_schema(const domain::Entity& entity) const;

    // Helper interne : convertit "list_by_fk_page" -> ("list_by_fk", "page")
    // Retourne nullopt si l'op_name n'est pas pagine.
    struct PaginationOpInfo {
        std::string op_base;  // "list", "list_by_fk", etc.
        std::string mode;     // "page", "offset", "cursor"
    };
    std::optional<PaginationOpInfo>
    parse_pagination_operation(const std::string& op_name) const;
    //------------------------------------------------------------------------



    // Conversion HTTP method
    std::string to_openapi_method(HttpMethod method) const;

    // Helper pour construire la security clause
    json bearer_security() const;

    // Génère le bloc requestBody approprié pour POST/PUT sur une entité :
    // - multipart/form-data si l'entité a au moins un champ File
    // - application/json sinon
    //
    // Schéma multipart : on génère un objet avec une propriété par field,
    // les fields File en 'string/binary' (le contenu du fichier upload)
    // et les autres champs en string (multipart est text-based).
    json make_request_body_content(const domain::Entity& entity) const;
    // ─────────────────────────────────────────────────────────────
    //  Helpers Access Control (RBAC + ABAC)
    // ─────────────────────────────────────────────────────────────

    /**
     * Enrichit un objet operation OpenAPI avec :
     *  - la réponse 403 Forbidden
     *  - une description Markdown des règles d'access_control
     *
     * Appelée pour chaque opération CRUD juste avant l'ajout au paths.
     */
    void enrich_with_access_control(
        json& op,
        const domain::Service& service,
        const std::string& entity_name,
        const std::string& operation_name
        ) const;

    /**
     * Trouve une entité dans le service par son nom.
     * Retourne nullptr si non trouvée.
     */
    const domain::Entity* find_entity_by_name(
        const domain::Service& service,
        const std::string& entity_name
        ) const;

    /**
     * Construit la description Markdown des règles d'autorisation
     * pour une opération CRUD donnée.
     *
     * Exemple de sortie :
     *   ### Access Control
     *   **Strategie**: Resource-aware (after DB load) - slow path
     *   **Regles** :
     *     - AND (toutes les conditions) :
     *       - `subject.roles intersects [admin, manager]`
     *       - `subject.attributes.department_id equals resource.attributes.department_id`
     */
    std::string build_authorization_description(
        const domain::Entity& entity,
        const std::string& operation_name
        ) const;
};

} // namespace sea::application