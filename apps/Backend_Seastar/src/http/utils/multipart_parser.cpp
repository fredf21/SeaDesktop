#include "multipart_parser.h"

#include <cctype>
#include <stdexcept>
#include <unordered_map>

namespace sea::http::utils::multipart {

namespace {

// ─────────────────────────────────────────────────────────────
// Utilitaires de chaîne — toutes binary-safe (std::string_view).
// ─────────────────────────────────────────────────────────────

// Compare deux strings en case-insensitive (ASCII uniquement).
bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

// Trim espaces/tabs/CR/LF de chaque côté.
std::string_view trim(std::string_view s) noexcept {
    auto is_ws = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    while (!s.empty() && is_ws(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && is_ws(static_cast<unsigned char>(s.back())))  s.remove_suffix(1);
    return s;
}

// Extrait la valeur d'un paramètre dans un header (ex: boundary=...).
// Gère les guillemets optionnels et la case-insensitivité sur le nom.
//
// Exemple :
//   header = "multipart/form-data; boundary=\"xyz\"; charset=utf-8"
//   key    = "boundary"
//   → "xyz"
//
// Retourne nullopt si non trouvé.
std::optional<std::string>
extract_header_param(std::string_view header, std::string_view key) {
    // On parse de gauche à droite. Approche : splitter sur ';',
    // puis pour chaque token chercher "key=...".
    std::size_t pos = 0;
    while (pos < header.size()) {
        std::size_t semi = header.find(';', pos);
        std::string_view token =
            (semi == std::string_view::npos)
                ? header.substr(pos)
                : header.substr(pos, semi - pos);
        token = trim(token);

        const std::size_t eq = token.find('=');
        if (eq != std::string_view::npos) {
            const auto k = trim(token.substr(0, eq));
            auto v = trim(token.substr(eq + 1));
            if (iequals(k, key)) {
                // Strip guillemets éventuels
                if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
                    v.remove_prefix(1);
                    v.remove_suffix(1);
                }
                return std::string(v);
            }
        }

        if (semi == std::string_view::npos) break;
        pos = semi + 1;
    }
    return std::nullopt;
}

// Parse un bloc de headers d'un part (ex: "Content-Disposition: form-data; name=\"x\"\r\n...")
// Retourne une map case-insensitive (clé → valeur).
struct PartHeaders {
    std::unordered_map<std::string, std::string> by_lower_name;

    [[nodiscard]] std::optional<std::string> get(std::string_view name) const {
        // Lookup case-insensitive : on normalise la clé à la lecture.
        std::string lower;
        lower.reserve(name.size());
        for (char c : name) {
            lower.push_back(static_cast<char>(
                std::tolower(static_cast<unsigned char>(c))));
        }
        auto it = by_lower_name.find(lower);
        if (it == by_lower_name.end()) return std::nullopt;
        return it->second;
    }
};

PartHeaders parse_part_headers(std::string_view headers_block) {
    PartHeaders ph{};
    std::size_t pos = 0;
    while (pos < headers_block.size()) {
        const std::size_t eol = headers_block.find("\r\n", pos);
        std::string_view line =
            (eol == std::string_view::npos)
                ? headers_block.substr(pos)
                : headers_block.substr(pos, eol - pos);

        const std::size_t colon = line.find(':');
        if (colon != std::string_view::npos) {
            std::string lower_name;
            for (char c : line.substr(0, colon)) {
                lower_name.push_back(static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c))));
            }
            const auto value = trim(line.substr(colon + 1));
            ph.by_lower_name[lower_name] = std::string(value);
        }

        if (eol == std::string_view::npos) break;
        pos = eol + 2;
    }
    return ph;
}

} // namespace

// ─────────────────────────────────────────────────────────────
// Helpers publics de ParsedMultipart
// ─────────────────────────────────────────────────────────────
const PartText*
ParsedMultipart::find_text(std::string_view name) const noexcept {
    for (const auto& p : text_parts) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

const PartFile*
ParsedMultipart::find_file(std::string_view name) const noexcept {
    for (const auto& p : file_parts) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

// ─────────────────────────────────────────────────────────────
// extract_boundary
// ─────────────────────────────────────────────────────────────
std::optional<std::string>
extract_boundary(std::string_view content_type) noexcept {
    // Récupère le type principal (avant le ';')
    const std::size_t semi = content_type.find(';');
    const auto main_type = trim(
        (semi == std::string_view::npos)
            ? content_type
            : content_type.substr(0, semi));

    if (!iequals(main_type, "multipart/form-data")) {
        return std::nullopt;
    }

    auto boundary = extract_header_param(content_type, "boundary");
    if (!boundary.has_value() || boundary->empty()) {
        return std::nullopt;
    }
    return boundary;
}

// ─────────────────────────────────────────────────────────────
// parse
//
// Structure d'un body multipart :
//
//   --BOUNDARY\r\n
//   Content-Disposition: form-data; name="field1"\r\n
//   \r\n
//   value1\r\n
//   --BOUNDARY\r\n
//   Content-Disposition: form-data; name="file"; filename="me.png"\r\n
//   Content-Type: image/png\r\n
//   \r\n
//   <binary>\r\n
//   --BOUNDARY--\r\n
//
// Note importante : les "\r\n" qui suivent un contenu de part font
// partie du delimiter, PAS du contenu. Donc le contenu d'un part
// s'étend jusqu'au "\r\n--BOUNDARY" suivant.
// ─────────────────────────────────────────────────────────────
ParsedMultipart parse(std::string_view body, const std::string& boundary)
{
    if (boundary.empty()) {
        throw std::runtime_error("multipart: boundary vide");
    }

    ParsedMultipart result{};

    // Construit les delimiters explicites.
    const std::string delim         = "--" + boundary;          // "--BOUNDARY"
    const std::string crlf_delim    = "\r\n--" + boundary;      // "\r\n--BOUNDARY"
    const std::string final_marker  = "--";                     // après le boundary final

    // Le body devrait commencer par "--BOUNDARY" (avec ou sans CRLF avant).
    // Certains clients préfixent par "\r\n" — on tolère.
    std::size_t cursor = 0;
    if (body.compare(0, delim.size(), delim) != 0) {
        // Tente de skipper d'éventuels octets parasites en amont du
        // premier delim.
        const auto first = body.find(delim);
        if (first == std::string_view::npos) {
            throw std::runtime_error("multipart: boundary introuvable en debut de body");
        }
        cursor = first;
    }
    cursor += delim.size();

    while (cursor < body.size()) {
        // Après le delimiter : soit "\r\n" (part suivant), soit "--\r\n"
        // (boundary final → fin du body).
        if (cursor + 1 < body.size() &&
            body[cursor] == '-' && body[cursor + 1] == '-') {
            // Boundary final : on a fini.
            break;
        }

        // Skip le "\r\n" qui suit le boundary.
        if (cursor + 1 < body.size() &&
            body[cursor] == '\r' && body[cursor + 1] == '\n') {
            cursor += 2;
        } else {
            throw std::runtime_error(
                "multipart: CRLF attendu apres le boundary (offset " +
                std::to_string(cursor) + ")");
        }

        // Lit les headers du part : jusqu'au double CRLF.
        const std::size_t hdr_end = body.find("\r\n\r\n", cursor);
        if (hdr_end == std::string_view::npos) {
            throw std::runtime_error("multipart: fin des headers de part introuvable");
        }
        const std::string_view headers_view =
            body.substr(cursor, hdr_end - cursor);
        const PartHeaders headers = parse_part_headers(headers_view);
        cursor = hdr_end + 4;   // skip le "\r\n\r\n"

        // Cherche le délimiteur suivant pour borner le contenu.
        const std::size_t content_end = body.find(crlf_delim, cursor);
        if (content_end == std::string_view::npos) {
            throw std::runtime_error("multipart: contenu non termine (boundary final manquant)");
        }
        std::string content_data{
            body.substr(cursor, content_end - cursor)
        };

        // Avance après le "\r\n--BOUNDARY"
        cursor = content_end + crlf_delim.size();

        // Analyse les headers du part pour décider type (texte vs fichier).
        const auto disposition = headers.get("Content-Disposition");
        if (!disposition.has_value()) {
            throw std::runtime_error(
                "multipart: Content-Disposition manquant sur un part");
        }

        const auto name_opt = extract_header_param(*disposition, "name");
        if (!name_opt.has_value() || name_opt->empty()) {
            throw std::runtime_error(
                "multipart: attribut name= manquant dans Content-Disposition");
        }

        const auto filename_opt = extract_header_param(*disposition, "filename");

        if (filename_opt.has_value()) {
            // Part fichier — même si filename est "" (cas d'un input file
            // soumis vide), on le traite comme file pour cohérence.
            PartFile pf{};
            pf.name         = *name_opt;
            pf.filename     = *filename_opt;
            pf.content_type = headers.get("Content-Type")
                                  .value_or("application/octet-stream");
            pf.content      = std::move(content_data);
            result.file_parts.push_back(std::move(pf));
        } else {
            // Part texte
            PartText pt{};
            pt.name  = *name_opt;
            pt.value = std::move(content_data);
            result.text_parts.push_back(std::move(pt));
        }
    }

    return result;
}

} // namespace sea::http::utils::multipart