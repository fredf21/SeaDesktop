#include "seed_orchestrator.h"

#include "mysql_introspector.h"
#include "mysql_schema_generator.h"
#include "spdlog/spdlog.h"

#include <bcrypt/BCrypt.hpp>

#include <seastar/core/coroutine.hh>

#include <iostream>
#include <sstream>
#include <variant>

namespace sea::infrastructure::persistence::mysql {

// ─────────────────────────────────────────────────────────────
// AliasRegistry implementation
// ─────────────────────────────────────────────────────────────
void AliasRegistry::set(const std::string& alias, const std::string& uuid)
{
    _map[alias] = uuid;
}

std::optional<std::string> AliasRegistry::get(const std::string& alias) const
{
    auto it = _map.find(alias);
    if (it == _map.end()) return std::nullopt;
    return it->second;
}

bool AliasRegistry::has(const std::string& alias) const
{
    return _map.find(alias) != _map.end();
}

// ─────────────────────────────────────────────────────────────
// SeedOrchestrator constructeur
// ─────────────────────────────────────────────────────────────
SeedOrchestrator::SeedOrchestrator(
    const sea::domain::DatabaseConfig& config,
    const sea::domain::Schema& schema,
    std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine> crud_engine,
    std::shared_ptr<MysqlIntrospector> introspector,
    std::shared_ptr<IBlockingExecutor> executor,
    std::shared_ptr<sea::infrastructure::persistence::IGenericRepository> repository)
    : _config(config)
    , _schema(schema)
    , _crud_engine(std::move(crud_engine))
    , _introspector(std::move(introspector))
    , _executor(std::move(executor))
    , _repository(std::move(repository))
{
}

// ─────────────────────────────────────────────────────────────
// bcrypt_hash_sync
// ─────────────────────────────────────────────────────────────
std::string SeedOrchestrator::bcrypt_hash_sync(const std::string& plain)
{
    return BCrypt::generateHash(plain);
}

// ─────────────────────────────────────────────────────────────
// hash_password_async
// ─────────────────────────────────────────────────────────────
seastar::future<std::string>
SeedOrchestrator::hash_password_async(const std::string& plain)
{
    co_return co_await _executor->submit(
        [plain]() {
            return bcrypt_hash_sync(plain);
        }
        );
}

// ─────────────────────────────────────────────────────────────
// should_seed_entity
// ─────────────────────────────────────────────────────────────
seastar::future<bool>
SeedOrchestrator::should_seed_entity(const sea::domain::Entity& entity)
{
    if (!entity.has_seeds()) {
        co_return false;
    }

    if (_config.migrations.seeds.mode == sea::domain::SeedsMode::Always) {
        co_return true;
    }

    const auto records = co_await _crud_engine->list(entity.name);

    if (!records.empty()) {
        spdlog::get("sea.persistence")->info(
            "SEEDS {}: skip (table not empty, {} row(s))",
            entity.name, records.size()
            );

        co_return false;
    }

    co_return true;
}

// ─────────────────────────────────────────────────────────────
// convert_seed_value_to_dynamic_value
// ─────────────────────────────────────────────────────────────
sea::infrastructure::runtime::DynamicValue
SeedOrchestrator::convert_seed_value_to_dynamic_value(
    const sea::domain::SeedValue& seed_value,
    const sea::domain::Field* field) const
{
    using DV = sea::infrastructure::runtime::DynamicValue;
    using FieldType = sea::domain::FieldType;

    if (std::holds_alternative<std::monostate>(seed_value)) {
        return std::monostate{};
    }

    if (std::holds_alternative<bool>(seed_value)) {
        return std::get<bool>(seed_value);
    }

    if (std::holds_alternative<std::int64_t>(seed_value)) {
        const auto i = std::get<std::int64_t>(seed_value);
        if (field == nullptr) return i;

        if (field->type == FieldType::Int
            || field->type == FieldType::SmallInt
            || field->type == FieldType::BigInt) {
            return i;
        }
        if (field->type == FieldType::Float) {
            return static_cast<double>(i);
        }
        return std::to_string(i);
    }

    if (std::holds_alternative<double>(seed_value)) {
        const auto d = std::get<double>(seed_value);
        if (field == nullptr) return d;

        if (field->type == FieldType::Float) {
            return d;
        }
        if (field->type == FieldType::Int
            || field->type == FieldType::SmallInt
            || field->type == FieldType::BigInt) {
            return static_cast<std::int64_t>(d);
        }
        return std::to_string(d);
    }

    if (std::holds_alternative<std::string>(seed_value)) {
        const auto& s = std::get<std::string>(seed_value);

        if (field == nullptr) return s;

        if (field->type == FieldType::Int
            || field->type == FieldType::SmallInt
            || field->type == FieldType::BigInt) {
            try {
                return static_cast<std::int64_t>(std::stoll(s));
            } catch (...) {
                return s;
            }
        }
        if (field->type == FieldType::Float) {
            try {
                return std::stod(s);
            } catch (...) {
                return s;
            }
        }
        if (field->type == FieldType::Bool) {
            return (s == "true" || s == "True" || s == "TRUE" || s == "1");
        }

        return s;
    }

    return std::monostate{};
}

// ─────────────────────────────────────────────────────────────
// resolve_string_value
// ─────────────────────────────────────────────────────────────
seastar::future<std::string>
SeedOrchestrator::resolve_string_value(
    const std::string& raw_value,
    const AliasRegistry& registry,
    const sea::domain::Field* field)
{
    // ${REF:alias}
    if (raw_value.size() > 7
        && raw_value.substr(0, 6) == "${REF:"
        && raw_value.back() == '}') {

        const std::string alias = raw_value.substr(6, raw_value.size() - 7);
        const auto uuid = registry.get(alias);

        if (uuid.has_value()) {
            co_return *uuid;
        } else {
            spdlog::get("sea.persistence")->warn(
                "SEEDS WARNING: alias '{}' not found in registry",
                alias
                );

            co_return raw_value;
        }
    }

    // {{hash:value}}
    if (raw_value.size() > 9
        && raw_value.substr(0, 7) == "{{hash:"
        && raw_value.substr(raw_value.size() - 2) == "}}") {

        const std::string plain = raw_value.substr(7, raw_value.size() - 9);

        if (_executor) {
            const auto hashed = co_await hash_password_async(plain);
            co_return hashed;
        } else {
            spdlog::get("sea.persistence")->warn(
                "[SEEDS] cannot hash password (no executor)"
                );

            co_return raw_value;
        }
    }

    co_return raw_value;
}

// ─────────────────────────────────────────────────────────────
// seed_entity_records
// ─────────────────────────────────────────────────────────────
seastar::future<>
SeedOrchestrator::seed_entity_records(
    const sea::domain::Entity& entity,
    AliasRegistry& registry,
    SeedResult& result)
{
    spdlog::get("sea.persistence")->info(
        "SEEDS {} : {} seed(s) to insert", entity.name, entity.seeds.size()
        );

    for (const auto& seed : entity.seeds) {
        sea::infrastructure::runtime::DynamicRecord record;

        for (const auto& [key, seed_value] : seed.values) {
            const auto* field = entity.find_field(key);
            auto dv = convert_seed_value_to_dynamic_value(seed_value, field);

            if (std::holds_alternative<std::string>(dv)) {
                const auto raw = std::get<std::string>(dv);
                const auto resolved = co_await resolve_string_value(raw, registry, field);
                record[key] = resolved;
            } else {
                record[key] = std::move(dv);
            }
        }
        const std::string alias_part = seed.has_alias()
                                           ? " [" + seed.alias + "]"
                                           : "";

        const auto op_result = co_await _crud_engine->create(entity.name, record);

        auto persist_log = spdlog::get("sea.persistence");

        if (!op_result.success) {
            persist_log->error(
                "  -{} fields={} FAILED",
                alias_part, seed.values.size()
                );
            for (const auto& err : op_result.errors) {
                persist_log->error("    error: {}", err);
                result.errors.push_back(
                    entity.name + "[" + seed.alias + "]: " + err
                    );
            }
            continue;
        }

        persist_log->info(
            "  -{} fields={} OK",
            alias_part, seed.values.size()
            );

        result.total_entities_seeded++;

        if (seed.has_alias() && op_result.record.has_value()) {
            auto id_it = op_result.record->find("id");
            if (id_it != op_result.record->end()) {
                if (std::holds_alternative<std::string>(id_it->second)) {
                    const auto& uuid = std::get<std::string>(id_it->second);
                    registry.set(seed.alias, uuid);
                }
                else if (std::holds_alternative<std::int64_t>(id_it->second)) {
                    const auto id_int = std::get<std::int64_t>(id_it->second);
                    registry.set(seed.alias, std::to_string(id_int));
                }
            }
        }
    }

    co_return;
}

// ─────────────────────────────────────────────────────────────
//  seed_m2m_pivots
//
// Pour chaque seed avec relations M2M :
// 1. Trouve la Relation dans entity.relations
// 2. Resolve source_alias (l'alias du seed) → UUID
// 3. Pour chaque target_alias :
//    - Resolve via registry → UUID
//    - INSERT INTO pivot_table via repository->insert_pivot()
// ─────────────────────────────────────────────────────────────
seastar::future<>
SeedOrchestrator::seed_m2m_pivots(
    const sea::domain::Entity& entity,
    const AliasRegistry& registry,
    SeedResult& result)
{
    for (const auto& seed : entity.seeds) {
        if (seed.m2m_relations.empty()) continue;

        // Verifie que le seed a un alias (sinon impossible de resolve la source)
        if (!seed.has_alias()) {
            spdlog::get("sea.persistence")->warn(
                "SEEDS M2M skip: seed in '{}' has no alias", entity.name
                );

            continue;
        }

        // Resolve l'alias source (du seed parent)
        const auto source_uuid = registry.get(seed.alias);
        if (!source_uuid.has_value()) {
            const auto err = "M2M source alias not found: " + seed.alias;
            spdlog::get("sea.persistence")->error("SEEDS {}", err);
            result.errors.push_back(err);
            continue;
        }

        // Pour chaque relation M2M dans ce seed
        for (const auto& [relation_name, target_aliases] : seed.m2m_relations) {

            // Trouve la Relation dans entity.relations
            const auto* relation = entity.find_relation(relation_name);
            if (relation == nullptr) {
                const auto err = "M2M relation not found in entity: "
                                 + entity.name + "." + relation_name;
                spdlog::get("sea.persistence")->error("SEEDS {}", err);
                result.errors.push_back(err);
                continue;
            }

            if (relation->kind != sea::domain::RelationKind::ManyToMany) {
                const auto err = "Relation is not M2M: "
                                 + entity.name + "." + relation_name;
                spdlog::get("sea.persistence")->error("SEEDS {}", err);
                result.errors.push_back(err);
                continue;
            }

            spdlog::get("sea.persistence")->info(
                "SEEDS M2M {}[{}].{}  ({} link(s))",
                entity.name, seed.alias, relation_name, target_aliases.size()
                );


            // Pour chaque target alias : resolve + INSERT pivot
            for (const auto& target_alias : target_aliases) {
                const auto target_uuid = registry.get(target_alias);

                if (!target_uuid.has_value()) {
                    const auto err = "M2M target alias not found: " + target_alias;
                     spdlog::get("sea.persistence")->error("SEEDS {}", err);
                    result.errors.push_back(err);
                    continue;
                }

                // Construit le record pivot : {source_fk: src_uuid, target_fk: tgt_uuid}
                sea::infrastructure::runtime::DynamicRecord pivot_record;
                pivot_record[relation->source_fk_column] = *source_uuid;
                pivot_record[relation->target_fk_column] = *target_uuid;

                // INSERT via repository
                const bool ok = co_await _repository->insert_pivot(
                    relation->pivot_table,
                    pivot_record
                    );

                if (ok) {
                    spdlog::get("sea.persistence")->info(
                        "  + {} <-> {} OK",
                        seed.alias, target_alias
                        );
                    result.total_pivot_rows++;
                } else {
                    const auto err = "Pivot insert failed: " + seed.alias
                                     + " <-> " + target_alias
                                     + " in " + relation->pivot_table;
                    spdlog::get("sea.persistence")->error("  ! {}", err);
                    result.errors.push_back(err);
                }

            }
        }
    }

    co_return;
}

// ─────────────────────────────────────────────────────────────
// seed_all (methode principale)
// ─────────────────────────────────────────────────────────────
seastar::future<SeedResult>
SeedOrchestrator::seed_all()
{
    SeedResult result;

    if (!_config.migrations.seeds.enabled) {
        spdlog::get("sea.persistence")->info(
            "SEEDS disabled (seeds.enabled=false)"
            );
        result.success = true;
        co_return result;
    }

    auto persist_log = spdlog::get("sea.persistence");
    persist_log->info("=== Starting seeds ===");
    persist_log->info("Mode: {}", to_string(_config.migrations.seeds.mode));
    persist_log->info("On error: {}", to_string(_config.migrations.seeds.on_error));


    const auto sorted_entities = MysqlSchemaGenerator::topological_sort(_schema.entities);

    AliasRegistry registry;

    // ── Passe 1 : entity seeds ──
    persist_log->info("Pass 1: inserting entity seeds");
    for (const auto* entity : sorted_entities) {
        if (!entity->has_seeds()) continue;

        const bool should_seed = co_await should_seed_entity(*entity);
        if (!should_seed) continue;

        co_await seed_entity_records(*entity, registry, result);
    }

    // ── Passe 2 : M2M pivots ──
    persist_log->info("Pass 2: inserting M2M pivot seeds");
    for (const auto* entity : sorted_entities) {
        if (!entity->has_seeds()) continue;
        co_await seed_m2m_pivots(*entity, registry, result);
    }

    // ── Resume ──
    persist_log->info("=== Summary ===");
    persist_log->info("Total entities seeded: {}", result.total_entities_seeded);
    persist_log->info("Total pivot rows: {}", result.total_pivot_rows);
    persist_log->info("Aliases registered: {}", registry.size());
    persist_log->info("Errors: {}", result.errors.size());
    for (const auto& e : result.errors) {
        persist_log->error("  ! {}", e);
    }


    result.success = result.errors.empty();
    if (result.success) {
        persist_log->info("=== SUCCESS ===");
    } else {
        persist_log->error("=== FAILED ===");
    }

    co_return result;
}

} // namespace sea::infrastructure::persistence::mysql