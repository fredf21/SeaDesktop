#pragma once

#include "pagination.h"

#include <seastar/http/request.hh>

#include <optional>
#include <string>

namespace sea::http::utils {

// ─────────────────────────────────────────────────────────────────────
// pagination_query
//
// Helpers de parsing et de validation des query params HTTP pour
// les routes paginees (3 modes : page, offset, cursor).
//
// Chaque mode a sa propre fonction parse_*_query() qui :
// 1. Lit les query params HTTP (page, page_size, limit, offset, sort, after)
// 2. Applique les defauts du PaginationConfig si absents
// 3. Valide contre le max_limit / max_page_size et la whitelist sortable_fields
// 4. Retourne soit un PageRequest/OffsetRequest/CursorRequest pret a passer
//    au repository, soit une erreur HTTP 400 a renvoyer au client.
//
// Le parsing du tri "field:direction" est mutualise via parse_sort_token().
// ─────────────────────────────────────────────────────────────────────

// Resultat d'un parse_*_query : soit une requete valide, soit une erreur
// (message d'erreur a inclure dans une reponse HTTP 400).
template <typename TRequest>
struct ParseResult {
    std::optional<TRequest>    request;   // present si OK
    std::optional<std::string> error;     // present si KO -> 400 Bad Request

    [[nodiscard]] bool ok() const noexcept { return request.has_value(); }
};

// Forward declarations pour eviter d'inclure i_generic_repository.h ici
namespace detail {
struct PageRequestRef;
struct OffsetRequestRef;
struct CursorRequestRef;
}

// ─────────────────────────────────────────────────────────────────────
// Parsing du tri "field:direction" (mutualise entre page et offset)
//
// Accepte :
//   "created_at:desc"            (simple)
//   "created_at:desc,email:asc"  (multi-tri)
//
// Pour le MVP, seul le PREMIER tri est utilise (le repository ne supporte
// qu'un seul ORDER BY field actuellement). Les suivants sont ignores.
//
// Validation :
// - le field doit etre dans 'allowed' (sortable_fields)
// - la direction doit etre "asc" ou "desc" (insensible a la casse)
//
// Retourne : (sort_field, sort_desc) ou nullopt si l'expression est
// vide / invalide / contient un field hors whitelist.
// ─────────────────────────────────────────────────────────────────────
struct SortToken {
    std::string field;
    bool        desc;
};

std::optional<SortToken>
parse_sort_expression(const std::string& expression,
                      const std::vector<std::string>& allowed,
                      std::string* error_out = nullptr);

} // namespace sea::http::utils

// ─────────────────────────────────────────────────────────────────────
// Surcharges typees par mode
//
// On les declare dans un namespace separe pour eviter une dependance
// circulaire pagination_query <-> i_generic_repository.
// ─────────────────────────────────────────────────────────────────────

#include "persistence/i_generic_repository.h"

namespace sea::http::utils {

// ─── Mode page-based ───────────────────────────────────────────────
//
// Lit : ?page=N&page_size=M&sort=field:dir
//
// Defauts : config.default_page_size, config.default_sort
// Bornes  : page_size <= config.max_page_size, page >= 1
// Sort    : doit etre dans config.sortable_fields
[[nodiscard]] ParseResult<sea::infrastructure::persistence::PageRequest>
parse_page_query(const seastar::http::request& req,
                 const sea::domain::PagePagination& config);


// ─── Mode offset/limit ─────────────────────────────────────────────
//
// Lit : ?offset=K&limit=M&sort=field:dir
//
// Defauts : config.default_limit, config.default_sort
// Bornes  : limit <= config.max_limit
// Sort    : doit etre dans config.sortable_fields
[[nodiscard]] ParseResult<sea::infrastructure::persistence::OffsetRequest>
parse_offset_query(const seastar::http::request& req,
                   const sea::domain::OffsetPagination& config);


// ─── Mode cursor ───────────────────────────────────────────────────
//
// Lit : ?after=<token>&limit=M
//
// Defauts : config.default_limit
// Bornes  : limit <= config.max_limit
// Cursor  : 'after' optionnel (premiere page si absent)
// Sort    : FIGE par le YAML (config.sort), pas lu depuis la requete
[[nodiscard]] ParseResult<sea::infrastructure::persistence::CursorRequest>
parse_cursor_query(const seastar::http::request& req,
                   const sea::domain::CursorPagination& config);

} // namespace sea::http::utils
