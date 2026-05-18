#include "pagination_query.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <string>
#include <string_view>
#include <vector>

namespace sea::http::utils {

namespace {

// Trim simple en place sur un string_view
[[nodiscard]] std::string_view trim_sv(std::string_view sv) {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) {
        sv.remove_suffix(1);
    }
    return sv;
}

// Lower-case en place sur une string
[[nodiscard]] std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Recupere un query param par nom, retourne nullopt s'il n'existe pas
// ou si sa valeur est vide.
//
// Note Seastar : on utilise req.get_query_param(name) (au SINGULIER)
// qui retourne une seastar::sstring. C'est l'API officielle Seastar
// pour acceder a un parametre par son nom.
//
// La methode get_query_params() (au pluriel) existe aussi mais
// retourne un std::vector<sstring> (liste des cles), inadapte ici.
//
// Conversion sstring -> std::string :
// seastar::basic_sstring expose un operator std::string_view() qui
// rend la conversion propre et portable entre versions de Seastar.
[[nodiscard]] std::optional<std::string>
get_query_param(const seastar::http::request& req, const std::string& name)
{
    const auto value = req.get_query_param(seastar::sstring(name));
    if (value.empty()) {
        return std::nullopt;
    }
    return std::string(std::string_view(value));
}

// Parse un std::size_t depuis une string ; retourne nullopt si invalide
// ou si la valeur depasse size_t.
[[nodiscard]] std::optional<std::size_t>
parse_size_t(const std::string& s)
{
    if (s.empty()) return std::nullopt;

    // Refuse les valeurs negatives ou avec signe
    if (s.front() == '-' || s.front() == '+') return std::nullopt;

    std::size_t value = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc{} || ptr != s.data() + s.size()) {
        return std::nullopt;
    }
    return value;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────
// parse_sort_expression
//
// "created_at:desc"            -> SortToken { "created_at", true }
// "created_at:desc,email:asc"  -> SortToken { "created_at", true } (prend le premier)
// "created_at"                 -> nullopt (manque la direction)
// "password:asc"               -> nullopt si "password" pas dans 'allowed'
// ─────────────────────────────────────────────────────────────────────
std::optional<SortToken>
parse_sort_expression(const std::string& expression,
                      const std::vector<std::string>& allowed,
                      std::string* error_out)
{
    if (expression.empty()) {
        if (error_out) *error_out = "L'expression de tri est vide.";
        return std::nullopt;
    }

    // Decoupe sur la premiere virgule (on ne supporte qu'un seul tri pour le MVP)
    std::string_view sv(expression);
    const auto comma = sv.find(',');
    std::string_view first = (comma == std::string_view::npos) ? sv : sv.substr(0, comma);
    first = trim_sv(first);

    if (first.empty()) {
        if (error_out) *error_out = "L'expression de tri est vide.";
        return std::nullopt;
    }

    const auto colon = first.find(':');
    if (colon == std::string_view::npos) {
        if (error_out) *error_out = "Tri mal forme : '" + std::string(first) +
                         "' (attendu 'field:asc' ou 'field:desc').";
        return std::nullopt;
    }

    SortToken token;
    token.field = std::string(trim_sv(first.substr(0, colon)));
    const std::string dir = to_lower(std::string(trim_sv(first.substr(colon + 1))));

    if (token.field.empty()) {
        if (error_out) *error_out = "Champ de tri vide.";
        return std::nullopt;
    }

    if (dir == "asc") {
        token.desc = false;
    } else if (dir == "desc") {
        token.desc = true;
    } else {
        if (error_out) *error_out = "Direction de tri invalide : '" + dir +
                         "' (attendu 'asc' ou 'desc').";
        return std::nullopt;
    }

    // Whitelist : le champ doit etre dans sortable_fields
    const bool whitelisted =
        std::find(allowed.begin(), allowed.end(), token.field) != allowed.end();
    if (!whitelisted) {
        if (error_out) *error_out = "Le champ de tri '" + token.field +
                         "' n'est pas autorise (pas dans sortable_fields).";
        return std::nullopt;
    }

    return token;
}

// ─────────────────────────────────────────────────────────────────────
// parse_page_query
// ─────────────────────────────────────────────────────────────────────
ParseResult<sea::infrastructure::persistence::PageRequest>
parse_page_query(const seastar::http::request& req,
                 const sea::domain::PagePagination& config)
{
    using sea::infrastructure::persistence::PageRequest;
    ParseResult<PageRequest> result;
    PageRequest request;

    // --- page ---
    const auto page_param = get_query_param(req, "page");
    if (page_param.has_value()) {
        const auto parsed = parse_size_t(*page_param);
        if (!parsed.has_value()) {
            result.error = "Le parametre 'page' doit etre un entier positif.";
            return result;
        }
        if (*parsed < 1) {
            result.error = "Le parametre 'page' doit etre >= 1.";
            return result;
        }
        request.page = *parsed;
    } else {
        request.page = 1;   // defaut
    }

    // --- page_size ---
    const auto size_param = get_query_param(req, "page_size");
    if (size_param.has_value()) {
        const auto parsed = parse_size_t(*size_param);
        if (!parsed.has_value()) {
            result.error = "Le parametre 'page_size' doit etre un entier positif.";
            return result;
        }
        if (*parsed == 0) {
            result.error = "Le parametre 'page_size' doit etre > 0.";
            return result;
        }
        if (*parsed > config.max_page_size) {
            result.error = "Le parametre 'page_size' depasse le maximum autorise (" +
                           std::to_string(config.max_page_size) + ").";
            return result;
        }
        request.page_size = *parsed;
    } else {
        request.page_size = config.default_page_size;
    }

    // --- sort ---
    std::string sort_expr;
    const auto sort_param = get_query_param(req, "sort");
    if (sort_param.has_value()) {
        sort_expr = *sort_param;
    } else if (config.default_sort.has_value()) {
        sort_expr = *config.default_sort;
    }

    if (!sort_expr.empty()) {
        std::string sort_error;
        const auto token = parse_sort_expression(sort_expr, config.sortable_fields, &sort_error);
        if (!token.has_value()) {
            result.error = "Tri invalide : " + sort_error;
            return result;
        }
        request.sort_field = token->field;
        request.sort_desc  = token->desc;
    }
    // Sinon : pas de tri (le repository renverra dans l'ordre naturel)

    result.request = std::move(request);
    return result;
}

// ─────────────────────────────────────────────────────────────────────
// parse_offset_query
// ─────────────────────────────────────────────────────────────────────
ParseResult<sea::infrastructure::persistence::OffsetRequest>
parse_offset_query(const seastar::http::request& req,
                   const sea::domain::OffsetPagination& config)
{
    using sea::infrastructure::persistence::OffsetRequest;
    ParseResult<OffsetRequest> result;
    OffsetRequest request;

    // --- offset ---
    const auto offset_param = get_query_param(req, "offset");
    if (offset_param.has_value()) {
        const auto parsed = parse_size_t(*offset_param);
        if (!parsed.has_value()) {
            result.error = "Le parametre 'offset' doit etre un entier >= 0.";
            return result;
        }
        request.offset = *parsed;
    } else {
        request.offset = 0;   // defaut
    }

    // --- limit ---
    const auto limit_param = get_query_param(req, "limit");
    if (limit_param.has_value()) {
        const auto parsed = parse_size_t(*limit_param);
        if (!parsed.has_value()) {
            result.error = "Le parametre 'limit' doit etre un entier positif.";
            return result;
        }
        if (*parsed == 0) {
            result.error = "Le parametre 'limit' doit etre > 0.";
            return result;
        }
        if (*parsed > config.max_limit) {
            result.error = "Le parametre 'limit' depasse le maximum autorise (" +
                           std::to_string(config.max_limit) + ").";
            return result;
        }
        request.limit = *parsed;
    } else {
        request.limit = config.default_limit;
    }

    // --- sort ---
    std::string sort_expr;
    const auto sort_param = get_query_param(req, "sort");
    if (sort_param.has_value()) {
        sort_expr = *sort_param;
    } else if (config.default_sort.has_value()) {
        sort_expr = *config.default_sort;
    }

    if (!sort_expr.empty()) {
        std::string sort_error;
        const auto token = parse_sort_expression(sort_expr, config.sortable_fields, &sort_error);
        if (!token.has_value()) {
            result.error = "Tri invalide : " + sort_error;
            return result;
        }
        request.sort_field = token->field;
        request.sort_desc  = token->desc;
    }

    result.request = std::move(request);
    return result;
}

// ─────────────────────────────────────────────────────────────────────
// parse_cursor_query
//
// Particularite : le tri est FIGE par le YAML (config.sort). On ne lit
// pas le query param 'sort' ici, contrairement aux deux autres modes.
// On parse simplement config.sort pour en extraire le cursor_field
// (deja garanti coherent par le schema_validator a l'etape 1).
// ─────────────────────────────────────────────────────────────────────
ParseResult<sea::infrastructure::persistence::CursorRequest>
parse_cursor_query(const seastar::http::request& req,
                   const sea::domain::CursorPagination& config)
{
    using sea::infrastructure::persistence::CursorRequest;
    ParseResult<CursorRequest> result;
    CursorRequest request;

    // --- after (optionnel) ---
    const auto after_param = get_query_param(req, "after");
    if (after_param.has_value()) {
        request.after = *after_param;
    }
    // Sinon : nullopt -> premiere page

    // --- limit ---
    const auto limit_param = get_query_param(req, "limit");
    if (limit_param.has_value()) {
        const auto parsed = parse_size_t(*limit_param);
        if (!parsed.has_value()) {
            result.error = "Le parametre 'limit' doit etre un entier positif.";
            return result;
        }
        if (*parsed == 0) {
            result.error = "Le parametre 'limit' doit etre > 0.";
            return result;
        }
        if (*parsed > config.max_limit) {
            result.error = "Le parametre 'limit' depasse le maximum autorise (" +
                           std::to_string(config.max_limit) + ").";
            return result;
        }
        request.limit = *parsed;
    } else {
        request.limit = config.default_limit;
    }

    // --- cursor_field + sort_desc (figes par le YAML) ---
    request.cursor_field = config.cursor_field;

    // Parse config.sort pour determiner la direction.
    // Note : le schema_validator garantit que config.sort est bien forme
    // ("field:asc" ou "field:desc"), donc on peut faire confiance ici.
    // On extrait juste la direction.
    const auto colon = config.sort.find(':');
    if (colon != std::string::npos) {
        const std::string dir = to_lower(config.sort.substr(colon + 1));
        request.sort_desc = (dir == "desc");
    } else {
        request.sort_desc = false;   // fallback safe
    }

    result.request = std::move(request);
    return result;
}

} // namespace sea::http::utils