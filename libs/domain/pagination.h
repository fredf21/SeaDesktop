#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace sea::domain {

// ─────────────────────────────────────────────────────────────
// Pagination
//
// Trois modes indépendants. Chacun peut être activé ou non,
// déclarés dans le YAML sous le bloc `pagination:` de l'entité.
//
//   pagination:
//     page:    { default_page_size, max_page_size, default_sort, sortable_fields }
//     offset:  { default_limit,     max_limit,     default_sort, sortable_fields }
//     cursor:  { default_limit,     max_limit,     cursor_field, sort }
//
// Un mode est activé ssi son sous-bloc est présent.
// Aucun bloc -> route /entity reste non paginée (comportement actuel).
// ─────────────────────────────────────────────────────────────

// Mode "page-based" : le client raisonne en page=N&page_size=M.
// Réponse: { items, page, page_size, total, total_pages, sort }
struct PagePagination {
    std::size_t                default_page_size = 20;
    std::size_t                max_page_size     = 100;
    std::optional<std::string> default_sort;        // ex: "created_at:desc"
    std::vector<std::string>   sortable_fields;     // whitelist anti-injection
};

// Mode "offset/limit" : le client raisonne en offset=K&limit=M.
// Réponse: { items, offset, limit, total, sort }
struct OffsetPagination {
    std::size_t                default_limit = 20;
    std::size_t                max_limit     = 100;
    std::optional<std::string> default_sort;        // ex: "created_at:desc"
    std::vector<std::string>   sortable_fields;     // whitelist anti-injection
};

// Mode "cursor" : le client raisonne en after=<token>&limit=M.
// Le tri est figé (sinon le cursor n'est plus stable).
// Réponse: { items, next_cursor, prev_cursor, limit }
struct CursorPagination {
    std::size_t default_limit = 20;
    std::size_t max_limit     = 100;
    std::string cursor_field;            // champ utilisé comme curseur (ex: "id")
    std::string sort;                    // ex: "id:asc" — figé
};

// Agrège les 3 modes possibles. La présence d'un std::optional traduit
// l'activation effective du mode pour cette entité.
struct PaginationConfig {
    std::optional<PagePagination>   page;
    std::optional<OffsetPagination> offset;
    std::optional<CursorPagination> cursor;

    [[nodiscard]] bool has_page()   const noexcept { return page.has_value(); }
    [[nodiscard]] bool has_offset() const noexcept { return offset.has_value(); }
    [[nodiscard]] bool has_cursor() const noexcept { return cursor.has_value(); }

    [[nodiscard]] bool any() const noexcept {
        return has_page() || has_offset() || has_cursor();
    }
};

} // namespace sea::domain