#pragma once

#include "database_config.h"
#include "schema.h"
#include "runtime/generic_crud_engine.h"
#include "runtime/schema_runtime_registry.h"
#include "thread_pool_execution/i_blocking_executor.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <seastar/core/future.hh>

namespace sea::infrastructure::persistence::mysql {

// Forward declaration
class MysqlIntrospector;

// ─────────────────────────────────────────────────────────────
// Resultat du seeding
// ─────────────────────────────────────────────────────────────
struct SeedResult {
    bool success = false;
    std::size_t total_entities_seeded = 0;
    std::size_t total_pivot_rows = 0;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

// ─────────────────────────────────────────────────────────────
// AliasRegistry : map alias YAML → UUID en DB
// ─────────────────────────────────────────────────────────────
class AliasRegistry {
public:
    void set(const std::string& alias, const std::string& uuid);
    [[nodiscard]] std::optional<std::string> get(const std::string& alias) const;
    [[nodiscard]] bool has(const std::string& alias) const;
    [[nodiscard]] std::size_t size() const noexcept { return _map.size(); }

private:
    std::unordered_map<std::string, std::string> _map;
};

// ─────────────────────────────────────────────────────────────
// SeedOrchestrator
//
// Orchestre l'insertion des seeds au boot du serveur.
//
// Phase Seeds.2 :
// - Resolution ${REF:alias} pour les FK
// - Hash des passwords {{hash:value}} avec BCrypt (en interne)
// - INSERT via CrudEngine.create()
// - Tracking alias → UUID
//
// Phase Seeds.3 :
// - Insertion des tables pivot M2M
// ─────────────────────────────────────────────────────────────
class SeedOrchestrator {
public:
    SeedOrchestrator(
        const sea::domain::DatabaseConfig& config,
        const sea::domain::Schema& schema,
        std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
        std::shared_ptr<MysqlIntrospector> introspector,
        std::shared_ptr<IBlockingExecutor> executor,
        std::shared_ptr<sea::infrastructure::persistence::IGenericRepository> repository
        );

    // Methode principale : execute le pipeline complet de seeding
    seastar::future<SeedResult> seed_all();

private:
    // Determine si on doit seeder (mode 'once' + table vide)
    seastar::future<bool> should_seed_entity(const sea::domain::Entity& entity);

    // Passe 1 : INSERT les seeds normaux
    seastar::future<>
    seed_entity_records(
        const sea::domain::Entity& entity,
        AliasRegistry& registry,
        SeedResult& result
        );

    // Passe 2 : INSERT les pivot tables M2M (Seeds.3)
    seastar::future<>
    seed_m2m_pivots(
        const sea::domain::Entity& entity,
        const AliasRegistry& registry,
        SeedResult& result
        );

    // Convertit un SeedValue YAML en DynamicValue runtime
    [[nodiscard]] sea::infrastructure::runtime::DynamicValue
    convert_seed_value_to_dynamic_value(
        const sea::domain::SeedValue& seed_value,
        const sea::domain::Field* field
        ) const;

    // Detecte et resoud ${REF:alias} et {{hash:value}} dans une string
    [[nodiscard]] seastar::future<std::string>
    resolve_string_value(
        const std::string& raw_value,
        const AliasRegistry& registry,
        const sea::domain::Field* field
        );

    // Hash BCrypt synchrone (CPU-bound, a appeler depuis l'executor)
    [[nodiscard]] static std::string
    bcrypt_hash_sync(const std::string& plain);

    // Hash BCrypt async via le blocking executor
    [[nodiscard]] seastar::future<std::string>
    hash_password_async(const std::string& plain);

    const sea::domain::DatabaseConfig& _config;
    const sea::domain::Schema& _schema;
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> _crud_engine;
    std::shared_ptr<MysqlIntrospector> _introspector;
    std::shared_ptr<IBlockingExecutor> _executor;
    std::shared_ptr<sea::infrastructure::persistence::IGenericRepository> _repository;
};

} // namespace sea::infrastructure::persistence::mysql