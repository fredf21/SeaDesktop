#pragma once

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

} // namespace sea::domain
