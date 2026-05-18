#pragma once
#include "access_control/access_control_config.h"

#include "database_config.h"
#include "logging/logging_config.h"
#include "schema.h"

#include <cstdint>     // std::uint16_t
#include <string>
#include <string_view>
#include "security_scheme/security_config.h"
#include "storage_config.h"
namespace sea::domain {

// ─────────────────────────────────────────────────────────────
// Configuration fonctionnelle d’un service généré/exécuté
// ─────────────────────────────────────────────────────────────
struct ServiceOptions {
    bool enable_logs       = true;   // logs runtime activés
    bool enable_metrics    = false;  // futur: métriques / monitoring
    bool enable_swagger    = false;  // futur: doc OpenAPI
    bool enable_healthcheck = true;  // endpoint /health possible plus tard
};

// ─────────────────────────────────────────────────────────────
// Service
//
// Représente une unité déployable de ton système.
// Exemple :
//
// Service "UserService"
//   - port 8080
//   - DatabaseConfig = Memory/PostgreSQL/MongoDB
//   - Schema = User, Role, Permission
//
// Dans le MVP :
// - un service correspond à un schéma chargé
// - il sera branché à un runtime générique
//
// Plus tard :
// - il pourra devenir un vrai service Seastar généré
// ─────────────────────────────────────────────────────────────
struct Service {
    std::string              name;                // ex: "UserService"
    std::uint16_t            port = 8080;        // port HTTP d'exposition

    security::SecurityConfig security;           // pour les middleware ratelimits httplimit cors scurity headers

    Schema                   schema;             // structure métier du service
    DatabaseConfig           database_config{};  // backend de persistence
    ServiceOptions           options{};          // options transverses

    access_control::AccessControlConfig access_control; // Pour l'ABAC
    // Par defaut : console texte, niveau info, async actif.
    // Configurable via section "logging:" dans le YAML.
    sea::domain::logging::LoggingConfig logging = sea::domain::logging::LoggingConfig::safe_defaults();
    // ─────────────────────────────────────────────────────────
    // Configuration du backend de stockage de fichiers.
    //
    // Optionnel : si nullopt, et que le schema n'a aucun champ
    // File, le FileService n'est pas instancie et aucune table
    // sea_files n'est creee.
    //
    // Si nullopt mais le schema a au moins un champ File, le
    // FileServiceFactory applique un fallback automatique :
    //   StorageConfig{ backend = Filesystem, root_path = "./uploads" }
    // (philosophie : tolerant pour demarrer rapidement).
    //
    // Pour personnaliser, ajouter un bloc `storage:` au niveau
    // service dans le YAML :
    //   storage:
    //     backend: filesystem
    //     root_path: /var/lib/seadesktop/uploads
    //     file_mode: 0640
    //     directory_mode: 0750
    // ─────────────────────────────────────────────────────────
    std::optional<StorageConfig> storage;

    // ── helpers ─────────────────────────────────────────────

    [[nodiscard]] bool has_valid_port() const noexcept {
        return port > 0;
    }

    [[nodiscard]] bool has_entities() const noexcept {
        return !schema.empty();
    }

    [[nodiscard]] bool uses_memory_database() const noexcept {
        return database_config.is_memory();
    }

    [[nodiscard]] bool uses_external_database() const noexcept {
        return database_config.requires_network_connection();
    }

    [[nodiscard]] const Entity* find_entity(std::string_view entity_name) const {
        return schema.find_entity(entity_name);
    }
    // Indique si le schema utilise au moins un champ File.
    // Delegue au helper du Schema (deduplication).
    [[nodiscard]] bool has_file_fields() const noexcept {
        return schema.has_file_fields();
    }


};

} // namespace sea::domain