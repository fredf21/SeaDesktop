#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace sea::domain {

// ─────────────────────────────────────────────────────────────
// Type de base de données utilisé par un Service
//
// Memory      : utilisé dans le MVP avec repository en mémoire
// PostgreSQL  : future base SQL
// MySQL       : future base SQL
// MongoDB     : future base NoSQL
// ─────────────────────────────────────────────────────────────
enum class DatabaseType {
    Memory,
    PostgreSQL,
    MongoDB,
    MySQL
};

// Conversion enum -> string
// Utile pour :
// - logs
// - export YAML
// - debug
constexpr std::string_view to_string(DatabaseType type) noexcept {
    switch (type) {
    case DatabaseType::Memory:     return "memory";
    case DatabaseType::PostgreSQL: return "postgres";
    case DatabaseType::MySQL:     return "mysql";
    case DatabaseType::MongoDB:    return "mongo";
    default:                       return "unknown";
    }
}

// ─────────────────────────────────────────────────────────────
// Mode de migration
//
// Conservative : ne fait que ADD (CREATE TABLE, ADD COLUMN).
//                Jamais DROP. Safe en production.
//
// Modified     : Conservative + MODIFY COLUMN + RENAME (V2).
//
// Aggressive   : Tout : ADD/DROP/MODIFY columns + DROP tables.
//                DANGER : peut perdre des donnees.
// ─────────────────────────────────────────────────────────────
enum class MigrationMode {
    Conservative,
    Modified,
    Aggressive
};

constexpr std::string_view to_string(MigrationMode mode) noexcept {
    switch (mode) {
    case MigrationMode::Conservative: return "conservative";
    case MigrationMode::Modified:     return "modified";
    case MigrationMode::Aggressive:   return "aggressive";
    default:                          return "unknown";
    }
}

inline std::optional<MigrationMode>
migration_mode_from_string(std::string_view s) noexcept;

// ─────────────────────────────────────────────────────────────
//  Configuration des migrations automatiques
// ─────────────────────────────────────────────────────────────
struct MigrationsConfig {
    // Active/desactive le bootstrapping au demarrage du serveur
    bool enabled = false;

    // Mode de migration (defaut : Conservative)
    MigrationMode mode = MigrationMode::Conservative;

    // Cree automatiquement la base de donnees si absente
    // (necessite une connexion sans 'database_name' specifie)
    bool create_database_if_missing = true;

    // Si true, lance le serveur en mode 'dry_run' :
    // affiche les SQL qui seraient executes sans les appliquer.
    bool dry_run = false;
};

// ─────────────────────────────────────────────────────────────
// Configuration de base de données d’un Service
//
// Cette structure est volontairement générale.
// Le MVP utilisera surtout DatabaseType::Memory,
// mais la structure est déjà prête pour PostgreSQL et MongoDB.
// ─────────────────────────────────────────────────────────────
struct DatabaseConfig {
    // Type de backend de persistence utilisé
    DatabaseType type = DatabaseType::Memory;

    // Paramètres réseau / connexion
    std::string host = "localhost";
    int         port = 0;

    // Identifiants de connexion
    std::string database_name;
    std::string username;
    std::string password;

    // Configuration des migrations automatiques (Phase A : CREATE TABLE + ADD COLUMN)
    MigrationsConfig migrations;

    // ── helpers ─────────────────────────────────────────────

    [[nodiscard]] bool is_memory() const noexcept {
        return type == DatabaseType::Memory;
    }

    [[nodiscard]] bool is_postgres() const noexcept {
        return type == DatabaseType::PostgreSQL;
    }

    [[nodiscard]] bool is_mongo() const noexcept {
        return type == DatabaseType::MongoDB;
    }

    [[nodiscard]] bool is_mysql() const noexcept {
        return type == DatabaseType::MySQL;
    }

    // Indique si une vraie connexion externe est requise
    [[nodiscard]] bool requires_network_connection() const noexcept {
        return type == DatabaseType::PostgreSQL || type == DatabaseType::MongoDB || type == DatabaseType::MySQL;
    }

    // Vérifie si la configuration minimale est présente
    // pour une DB externe.
    [[nodiscard]] bool has_connection_info() const noexcept {
        if (is_memory()) {
            return true;
        }

        return !host.empty()
               && port > 0
               && !database_name.empty();
    }
};

// ─────────────────────────────────────────────────────────────
// Implementation de migration_mode_from_string
// (apres struct pour eviter forward declaration)
// ─────────────────────────────────────────────────────────────
inline std::optional<MigrationMode>
migration_mode_from_string(std::string_view s) noexcept
{
    std::string lower{s};
    for (auto& c : lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    if (lower == "conservative") return MigrationMode::Conservative;
    if (lower == "modified")     return MigrationMode::Modified;
    if (lower == "aggressive")   return MigrationMode::Aggressive;

    return std::nullopt;
}

} // namespace sea::domain