#pragma once

// ─────────────────────────────────────────────────────────────
// MultipartParser
//
// Parser RFC 7578 simplifié pour multipart/form-data. Conçu pour
// l'upload de fichiers depuis des formulaires HTML/HTTP clients
// classiques.
//
// SCOPE (ce qu'on supporte) :
//   - Content-Type: multipart/form-data; boundary=xxx
//   - Parts avec headers Content-Disposition + Content-Type
//   - filename="..." (champ File) et name="..." (champ texte)
//   - Boundary final (--xxx--)
//
// HORS SCOPE (ce qu'on ne supporte PAS) :
//   - nested multipart (multipart/mixed à l'intérieur d'un part)
//   - encoding quoted-printable / base64 dans les parts
//   - charset / Content-Transfer-Encoding
//   - filename* (RFC 5987 extended)
//
// Ces limitations couvrent 95%+ des clients HTTP réels (navigateurs,
// curl, Postman) et restent simples à maintenir.
//
// SÉCURITÉ :
//   - Le parser est appelé après que read_request_body() a déjà
//     appliqué http_limits.max_body_size. Pas de risque de RAM
//     exhaustion.
//   - Les parts sont copiés tels quels (binary-safe via std::string).
//   - Le caller doit valider chaque PartFile via FileService::validate_upload
//     avant de toucher au storage.
// ─────────────────────────────────────────────────────────────

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sea::http::utils::multipart {

// Un "part" représentant un champ texte simple (input type=text).
struct PartText {
    std::string name;     // valeur de name="..." dans Content-Disposition
    std::string value;    // contenu du part (binary-safe)
};

// Un "part" représentant un fichier upload (input type=file).
struct PartFile {
    std::string name;          // valeur de name="..."
    std::string filename;      // valeur de filename="..."
    std::string content_type;  // valeur du header Content-Type du part ("application/octet-stream" si absent)
    std::string content;       // contenu binaire du fichier
};

// Résultat global du parsing.
struct ParsedMultipart {
    std::vector<PartText> text_parts;
    std::vector<PartFile> file_parts;

    // ── helpers ─────────────────────────────────────────────

    // Cherche un text part par son nom (premier match).
    [[nodiscard]] const PartText* find_text(std::string_view name) const noexcept;

    // Cherche un file part par son nom (premier match).
    [[nodiscard]] const PartFile* find_file(std::string_view name) const noexcept;
};

// ─────────────────────────────────────────────────────────────
// extract_boundary
//
// Extrait la valeur du boundary depuis un header Content-Type.
// Accepte les formes :
//   "multipart/form-data; boundary=xxx"
//   "multipart/form-data; boundary=\"xxx\""
//   "multipart/form-data;boundary=xxx" (sans espace)
//   "Multipart/Form-Data; BOUNDARY=xxx" (case-insensitive sur tokens)
//
// Retourne nullopt si :
//   - le content_type n'est pas un multipart/form-data
//   - aucun boundary= n'est trouvé
//   - le boundary est vide
// ─────────────────────────────────────────────────────────────
[[nodiscard]] std::optional<std::string>
extract_boundary(std::string_view content_type) noexcept;

// ─────────────────────────────────────────────────────────────
// parse
//
// Parse un body multipart et retourne la liste des parts texte
// et fichiers.
//
// Lève std::runtime_error si :
//   - le body est mal formé (pas de boundary, terminaison manquante)
//   - un part n'a pas de Content-Disposition: form-data
//   - un part n'a pas d'attribut name=
//
// Le boundary fourni est sans les "--" du préfixe (extract_boundary
// retire déjà la mise en forme).
// ─────────────────────────────────────────────────────────────
[[nodiscard]] ParsedMultipart
parse(std::string_view body, const std::string& boundary);

} // namespace sea::http::utils::multipart