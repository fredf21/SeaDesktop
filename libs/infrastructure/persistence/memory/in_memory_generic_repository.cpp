#include "in_memory_generic_repository.h"
#include "spdlog/spdlog.h"

#include <variant>
#include <algorithm>
#include <seastar/core/coroutine.hh>

namespace sea::infrastructure::persistence {

namespace {

/**
 * Convertit un DynamicValue en string.
 *
 * Utilisé pour :
 * - comparer des champs (find_one_by_field)
 * - générer des clés pivot
 */
std::optional<std::string> dynamic_value_to_string(const runtime::DynamicValue& value)
{
    if (std::holds_alternative<std::string>(value)) {
        return std::get<std::string>(value);
    }
    if (std::holds_alternative<std::int64_t>(value)) {
        return std::to_string(std::get<std::int64_t>(value));
    }
    if (std::holds_alternative<double>(value)) {
        return std::to_string(std::get<double>(value));
    }
    if (std::holds_alternative<bool>(value)) {
        return std::get<bool>(value) ? "true" : "false";
    }

    return std::nullopt;
}

/**
 * Génère une clé unique pour une relation pivot (many-to-many).
 *
 * Exemple :
 * { user_id=1, role_id=2 } → "role_id=2;user_id=1;"
 *
 * Important :
 * - tri des clés → ordre stable
 */
std::string make_record_key(const runtime::DynamicRecord& values)
{
    std::vector<std::string> keys;
    keys.reserve(values.size());

    for (const auto& [key, _] : values) {
        keys.push_back(key);
    }

    std::sort(keys.begin(), keys.end());

    std::string result;

    for (const auto& key : keys) {
        auto value_opt = dynamic_value_to_string(values.at(key));

        if (!value_opt.has_value()) {
            continue; // sécurité
        }

        result += key;
        result += "=";
        result += *value_opt;
        result += ";";
    }

    return result;
}

// ─────────────────────────────────────────────────────────────────
// Helpers pagination : comparaison de DynamicValue pour le tri
//
// On compare typiquement des entiers, doubles, strings, bools.
// Si les types diffèrent ou ne sont pas comparables, on retombe
// sur leur représentation string (stable mais pas toujours sémantique).
// ─────────────────────────────────────────────────────────────────
int compare_dynamic_values(const runtime::DynamicValue& a,
                           const runtime::DynamicValue& b)
{
    // Si les deux sont des int64 → comparaison numérique
    if (std::holds_alternative<std::int64_t>(a) &&
        std::holds_alternative<std::int64_t>(b)) {
        const auto av = std::get<std::int64_t>(a);
        const auto bv = std::get<std::int64_t>(b);
        if (av < bv) return -1;
        if (av > bv) return 1;
        return 0;
    }
    // Si les deux sont des double
    if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b)) {
        const auto av = std::get<double>(a);
        const auto bv = std::get<double>(b);
        if (av < bv) return -1;
        if (av > bv) return 1;
        return 0;
    }
    // Si les deux sont des bool
    if (std::holds_alternative<bool>(a) && std::holds_alternative<bool>(b)) {
        const auto av = std::get<bool>(a);
        const auto bv = std::get<bool>(b);
        if (av == bv) return 0;
        return av ? 1 : -1;   // false < true
    }
    // Fallback : comparaison string
    const auto as = dynamic_value_to_string(a);
    const auto bs = dynamic_value_to_string(b);
    if (!as.has_value() && !bs.has_value()) return 0;
    if (!as.has_value()) return -1;
    if (!bs.has_value()) return 1;
    if (*as < *bs) return -1;
    if (*as > *bs) return 1;
    return 0;
}

} // namespace

/**
 * Extrait l'ID depuis un record.
 *
 * Supporte :
 * - string
 * - int64
 */
std::optional<std::string>
InMemoryGenericRepository::extract_id(const runtime::DynamicRecord& record) const
{
    const auto it = record.find("id");
    if (it == record.end()) return std::nullopt;

    if (std::holds_alternative<std::string>(it->second))
        return std::get<std::string>(it->second);

    if (std::holds_alternative<std::int64_t>(it->second))
        return std::to_string(std::get<std::int64_t>(it->second));

    return std::nullopt;
}

/**
 * CREATE
 *
 * Ajoute un record en mémoire.
 *
 * Règles :
 * - ID obligatoire
 * - pas d’overwrite silencieux
 * - ignore les champs many-to-many
 */
seastar::future<std::optional<runtime::DynamicRecord>>
InMemoryGenericRepository::create(const std::string& entity_name,
                                  runtime::DynamicRecord record)
{
    const auto id = extract_id(record);

    if (!id.has_value()) {
        return seastar::make_ready_future<std::optional<runtime::DynamicRecord>>(std::nullopt);
    }

    auto& entity_storage = storage_[entity_name];

    // Empêche écrasement silencieux
    if (entity_storage.contains(*id)) {
        return seastar::make_ready_future<std::optional<runtime::DynamicRecord>>(std::nullopt);
    }

    runtime::DynamicRecord filtered_record;

    for (auto& [key, value] : record) {
        // Ignore les relations many-to-many
        if (std::holds_alternative<std::vector<std::string>>(value)) {
            continue;
        }

        filtered_record[key] = std::move(value);
    }

    entity_storage[*id] = filtered_record;

    return seastar::make_ready_future<std::optional<runtime::DynamicRecord>>(filtered_record);
}

/**
 * FIND ALL
 */
seastar::future<std::vector<runtime::DynamicRecord>>
InMemoryGenericRepository::find_all(const std::string& entity_name)
{
    std::vector<runtime::DynamicRecord> result;

    const auto it = storage_.find(entity_name);
    if (it == storage_.end()) {
        return seastar::make_ready_future<std::vector<runtime::DynamicRecord>>(result);
    }

    result.reserve(it->second.size());

    for (const auto& [_, record] : it->second) {
        result.push_back(record);
    }

    return seastar::make_ready_future<std::vector<runtime::DynamicRecord>>(result);
}

/**
 * FIND BY ID
 */
seastar::future<std::optional<runtime::DynamicRecord>>
InMemoryGenericRepository::find_by_id(const std::string& entity_name,
                                      const std::string& id)
{
    const auto it = storage_.find(entity_name);
    if (it == storage_.end()) {
        return seastar::make_ready_future<std::optional<runtime::DynamicRecord>>(std::nullopt);
    }

    const auto rec_it = it->second.find(id);
    if (rec_it == it->second.end()) {
        return seastar::make_ready_future<std::optional<runtime::DynamicRecord>>(std::nullopt);
    }

    return seastar::make_ready_future<std::optional<runtime::DynamicRecord>>(rec_it->second);
}

/**
 * FIND ONE BY FIELD
 */
seastar::future<std::optional<runtime::DynamicRecord>>
InMemoryGenericRepository::find_one_by_field(const std::string& entity_name,
                                             const std::string& field_name,
                                             const std::string& value)
{
    const auto entity_it = storage_.find(entity_name);
    if (entity_it == storage_.end()) {
        return seastar::make_ready_future<std::optional<runtime::DynamicRecord>>(std::nullopt);
    }

    for (const auto& [_, record] : entity_it->second) {
        const auto field_it = record.find(field_name);
        if (field_it == record.end()) continue;

        const auto field_value = dynamic_value_to_string(field_it->second);

        if (field_value.has_value() && *field_value == value) {
            return seastar::make_ready_future<std::optional<runtime::DynamicRecord>>(record);
        }
    }

    return seastar::make_ready_future<std::optional<runtime::DynamicRecord>>(std::nullopt);
}

/**
 * DELETE
 */
seastar::future<bool>
InMemoryGenericRepository::remove(const std::string& entity_name,
                                  const std::string& id)
{
    const auto it = storage_.find(entity_name);
    if (it == storage_.end()) {
        return seastar::make_ready_future<bool>(false);
    }

    return seastar::make_ready_future<bool>(it->second.erase(id) > 0);
}

/**
 * UPDATE
 */
seastar::future<UpdateResponse>
InMemoryGenericRepository::update(const std::string& entity_name,
                                  const std::string& id,
                                  runtime::DynamicRecord record)
{
    UpdateResponse response;

    const auto it = storage_.find(entity_name);
    if (it == storage_.end()) {
        return seastar::make_ready_future<UpdateResponse>(response);
    }

    const auto rec_it = it->second.find(id);
    if (rec_it == it->second.end()) {
        return seastar::make_ready_future<UpdateResponse>(response);
    }

    auto& existing = rec_it->second;

    for (auto& [key, value] : record) {
        if (key == "id") continue;

        // Ignore many-to-many
        if (std::holds_alternative<std::vector<std::string>>(value)) {
            continue;
        }

        existing[key] = std::move(value);
    }

    response.record = existing;
    response.status = true;

    return seastar::make_ready_future<UpdateResponse>(response);
}

/**
 * INSERT PIVOT
 *
 * Simule une table pivot en mémoire.
 */
seastar::future<bool>
InMemoryGenericRepository::insert_pivot(const std::string& pivot_table,
                                        runtime::DynamicRecord values)
{
    auto& pivot_storage = storage_[pivot_table];

    const std::string synthetic_id = make_record_key(values);

    pivot_storage[synthetic_id] = std::move(values);

    return seastar::make_ready_future<bool>(true);
}

/**
 * Pour le backend Memory, "in_transaction" est essentiellement un no-op :
 * - Pas de vraie ACID en memoire
 * - Mais on respecte le contrat : execute la lambda et retourne le resultat
 *
 * Note : si la lambda retourne false ou throw, on NE peut PAS rollback les
 * modifications deja faites (pas de snapshot). C'est une limitation acceptable
 * pour un backend de dev/test.
 */
seastar::future<sea::infrastructure::persistence::TransactionResult>
InMemoryGenericRepository::in_transaction(std::function<seastar::future<bool>()> work)
{
    bool committed = false;
    std::string error_message;

    try {
        committed = co_await work();
        if (!committed) {
            error_message = "Transaction returned false (no real rollback in Memory backend)";
        }
    } catch (const std::exception& e) {
        committed = false;
        error_message = std::string("Exception in Memory transaction: ") + e.what();
    } catch (...) {
        committed = false;
        error_message = "Unknown exception in Memory transaction";
    }

    co_return sea::infrastructure::persistence::TransactionResult{
        .committed = committed,
        .error_message = std::move(error_message)
    };
}

// ═══════════════════════════════════════════════════════════════════
// PAGINATION
// ═══════════════════════════════════════════════════════════════════

/**
 * Helper : collecte tous les records d'une entité, triés.
 *
 * Si sort_field est nullopt, l'ordre dépend de l'itération du
 * unordered_map (non garanti stable). Pour un backend de test
 * c'est acceptable. Sinon on trie via compare_dynamic_values.
 */
std::vector<runtime::DynamicRecord>
InMemoryGenericRepository::collect_all_sorted(
    const std::string& entity_name,
    const std::optional<std::string>& sort_field,
    bool sort_desc) const
{
    std::vector<runtime::DynamicRecord> rows;

    const auto it = storage_.find(entity_name);
    if (it == storage_.end()) {
        return rows;
    }

    rows.reserve(it->second.size());
    for (const auto& [_, record] : it->second) {
        rows.push_back(record);
    }

    if (sort_field.has_value()) {
        const std::string& field = *sort_field;
        std::sort(rows.begin(), rows.end(),
                  [&field, sort_desc](const runtime::DynamicRecord& a,
                                      const runtime::DynamicRecord& b) {
                      const auto ait = a.find(field);
                      const auto bit = b.find(field);

                      // Les records sans le champ tombent en fin
                      if (ait == a.end() && bit == b.end()) return false;
                      if (ait == a.end()) return false;
                      if (bit == b.end()) return true;

                      const int cmp = compare_dynamic_values(ait->second, bit->second);
                      return sort_desc ? (cmp > 0) : (cmp < 0);
                  });
    }

    return rows;
}

seastar::future<std::size_t>
InMemoryGenericRepository::count(const std::string& entity_name)
{
    const auto it = storage_.find(entity_name);
    if (it == storage_.end()) {
        return seastar::make_ready_future<std::size_t>(0);
    }
    return seastar::make_ready_future<std::size_t>(it->second.size());
}

seastar::future<PageResult>
InMemoryGenericRepository::list_page(const std::string& entity_name,
                                     const PageRequest& request)
{
    PageResult result;

    auto rows = collect_all_sorted(entity_name, request.sort_field, request.sort_desc);
    result.total = rows.size();

    // page 1-indexee → offset = (page - 1) * page_size
    // page = 0 est traite comme page = 1 (defensive)
    const std::size_t page = request.page > 0 ? request.page : 1;
    const std::size_t offset = (page - 1) * request.page_size;

    if (offset >= rows.size() || request.page_size == 0) {
        return seastar::make_ready_future<PageResult>(std::move(result));
    }

    const std::size_t end = std::min(offset + request.page_size, rows.size());
    result.items.reserve(end - offset);
    for (std::size_t i = offset; i < end; ++i) {
        result.items.push_back(std::move(rows[i]));
    }

    return seastar::make_ready_future<PageResult>(std::move(result));
}

seastar::future<OffsetResult>
InMemoryGenericRepository::list_offset(const std::string& entity_name,
                                       const OffsetRequest& request)
{
    OffsetResult result;

    auto rows = collect_all_sorted(entity_name, request.sort_field, request.sort_desc);
    result.total = rows.size();

    if (request.offset >= rows.size() || request.limit == 0) {
        return seastar::make_ready_future<OffsetResult>(std::move(result));
    }

    const std::size_t end = std::min(request.offset + request.limit, rows.size());
    result.items.reserve(end - request.offset);
    for (std::size_t i = request.offset; i < end; ++i) {
        result.items.push_back(std::move(rows[i]));
    }

    return seastar::make_ready_future<OffsetResult>(std::move(result));
}

seastar::future<CursorResult>
InMemoryGenericRepository::list_cursor(const std::string& entity_name,
                                       const CursorRequest& request)
{
    CursorResult result;

    // Le cursor impose le tri par cursor_field
    auto rows = collect_all_sorted(entity_name,
                                   std::optional<std::string>(request.cursor_field),
                                   request.sort_desc);

    if (rows.empty() || request.limit == 0) {
        return seastar::make_ready_future<CursorResult>(std::move(result));
    }

    // Detection du point de depart selon 'after'
    std::size_t start = 0;
    if (request.after.has_value()) {
        const std::string& after = *request.after;
        // On cherche le premier element STRICTEMENT apres la valeur 'after'
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const auto it = rows[i].find(request.cursor_field);
            if (it == rows[i].end()) continue;
            const auto sval = dynamic_value_to_string(it->second);
            if (!sval.has_value()) continue;

            // En tri ASC : on saute tout ce qui est <= after
            // En tri DESC : on saute tout ce qui est >= after
            const bool past_cursor =
                request.sort_desc ? (*sval < after) : (*sval > after);
            if (past_cursor) {
                start = i;
                break;
            }
            // Si on arrive a la fin sans rien trouver, start reste a rows.size()
            if (i == rows.size() - 1) {
                start = rows.size();
            }
        }
    }

    if (start >= rows.size()) {
        return seastar::make_ready_future<CursorResult>(std::move(result));
    }

    const std::size_t end = std::min(start + request.limit, rows.size());
    result.items.reserve(end - start);
    for (std::size_t i = start; i < end; ++i) {
        result.items.push_back(rows[i]);
    }

    // next_cursor = valeur du cursor_field du dernier element si plus de pages
    if (end < rows.size()) {
        const auto& last = result.items.back();
        const auto it = last.find(request.cursor_field);
        if (it != last.end()) {
            const auto sval = dynamic_value_to_string(it->second);
            if (sval.has_value()) {
                result.next_cursor = *sval;
            }
        }
    }

    return seastar::make_ready_future<CursorResult>(std::move(result));
}
// ─────────────────────────────────────────────────────────────
// increment_field
//
// En in-memory, on lit la valeur courante du champ, on l'incrémente,
// et on remet la valeur dans le record. Mono-shard Seastar = pas de
// race condition (single-threaded).
//
// On supporte les variants entiers (int16/32/64, signés/non-signés).
// Les autres types (string, double, ...) renvoient false : le contrat
// d'increment_field exige un champ numérique.
// ─────────────────────────────────────────────────────────────
seastar::future<bool>
InMemoryGenericRepository::increment_field(const std::string& entity_name,
                                           const std::string& id,
                                           const std::string& field_name,
                                           std::int64_t delta)
{
    const auto entity_it = storage_.find(entity_name);
    if (entity_it == storage_.end()) {
        return seastar::make_ready_future<bool>(false);
    }

    const auto rec_it = entity_it->second.find(id);
    if (rec_it == entity_it->second.end()) {
        return seastar::make_ready_future<bool>(false);
    }

    auto field_it = rec_it->second.find(field_name);
    if (field_it == rec_it->second.end()) {
        return seastar::make_ready_future<bool>(false);
    }

    // Visite typée : seuls les variants entiers sont valides.
    auto& value = field_it->second;
    bool incremented = std::visit(
        [delta](auto& v) -> bool {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::int64_t> ||
                          std::is_same_v<T, std::int32_t> ||
                          std::is_same_v<T, std::int16_t>) {
                v = static_cast<T>(v + delta);
                return true;
            } else if constexpr (std::is_same_v<T, std::uint64_t> ||
                                 std::is_same_v<T, std::uint32_t> ||
                                 std::is_same_v<T, std::uint16_t>) {
                // Pour les unsigned, on cast prudemment ; un delta
                // négatif qui underflowerait le champ retournerait
                // false dans le SGBD MySQL aussi (BIGINT UNSIGNED).
                if (delta < 0 && static_cast<std::uint64_t>(-delta) > v) {
                    return false;
                }
                v = static_cast<T>(static_cast<std::int64_t>(v) + delta);
                return true;
            } else {
                return false;
            }
        },
        value);

    return seastar::make_ready_future<bool>(incremented);
}
// Le backend in-memory ne supporte pas les pivots (utile uniquement
// pour les tests). Les stubs retournent false et logguent un warning.

seastar::future<bool>
InMemoryGenericRepository::delete_pivot(
    const std::string& pivot_table,
    runtime::DynamicRecord values)
{
    (void)pivot_table;
    (void)values;
    spdlog::get("sea.persistence")->warn(
        "InMemoryGenericRepository::delete_pivot not implemented");
    return seastar::make_ready_future<bool>(false);
}

seastar::future<bool>
InMemoryGenericRepository::pivot_exists(
    const std::string& pivot_table,
    runtime::DynamicRecord values)
{
    (void)pivot_table;
    (void)values;
    spdlog::get("sea.persistence")->warn(
        "InMemoryGenericRepository::pivot_exists not implemented");
    return seastar::make_ready_future<bool>(false);
}

} // namespace sea::infrastructure::persistence