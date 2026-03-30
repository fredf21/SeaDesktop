#pragma once
#pragma once

#include "database_config.h"
#include "schema.h"

#include <cstdint>     // std::uint16_t
#include <string>
#include <string_view>

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
    std::string    name;               // ex: "UserService"
    std::uint16_t  port = 8080;        // port HTTP d'exposition

    Schema         schema;             // structure métier du service
    DatabaseConfig database_config{};  // backend de persistence
    ServiceOptions options{};          // options transverses

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
};

} // namespace sea::domain