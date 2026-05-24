#pragma once

#include "access_control/entity_access_control.h"
#include "field.h"
#include "pagination.h"
#include "relation.h"

#include <string>
#include <vector>
#include <algorithm>

namespace sea::domain {

struct EntityOptions {
    bool enable_crud       = true;   // génère GET/POST/PUT/DELETE
    bool is_auth_source       = false;  // génère /register /login /token
    bool enable_websocket  = false;  // génère ws://<entity>/live
    bool soft_delete       = false;  // ajoute deleted_at, ne supprime pas vraiment
    bool timestamps        = true;   // ajoute created_at / updated_at automatiquement
    bool public_routes    = false;   // Defini si une route est protege par le middleware de securitE
};


// ─────────────────────────────────────────────────────────────
// SeedRecord : un enregistrement a inserer comme seed
//
// Contient :
// - alias : optionnel, sert a referencer ce record depuis d'autres seeds
//           via ${REF:alias}
// - values : map cle/valeur des champs (resolus par le SeedOrchestrator)
// - m2m_relations : map nom_relation → liste d'aliases cibles
//                   (Phase Seeds.3 : pour la table pivot)
//
// Les valeurs peuvent contenir des macros :
// - ${REF:alias}     → resolu a l'UUID de l'entite avec cet alias
// - {{hash:value}}   → bcrypt du value (pour les fields Password)
// ─────────────────────────────────────────────────────────────
using SeedValue = std::variant<
    std::monostate,
    std::string,
    std::int64_t,
    std::int32_t,
    std::uint64_t,
    std::uint32_t,
    double,
    bool
    >;

struct SeedRecord {
    std::string alias;                              // optionnel
    std::map<std::string, SeedValue> values;        // champs simples
    std::map<std::string, std::vector<std::string>> m2m_relations;
    // ↑ key = nom de relation (ex: "programs"), value = liste d'aliases

    [[nodiscard]] bool has_alias() const noexcept {
        return !alias.empty();
    }
};

struct Entity {
    std::string            name;        // ex: "User"  (FredericCase)
    std::string            table_name;  // ex: "users" (calculé si vide)
    std::vector<Field>     fields;
    std::vector<Relation>  relations;
    EntityOptions          options;
    access_control::EntityAccessControl access_control;
    // Seeds.1 : seeds optionnels
    std::vector<SeedRecord> seeds;
    std::optional<PaginationConfig> pagination;  // nullopt => pas de routes paginées
    // ── helpers ────────────────────────────────────────────────

#include <algorithm>
#include <cctype>
#include <string>

    [[nodiscard]] static std::string to_route_plural(std::string name, bool must_be_plural = true)
    {
        if (name.empty()) {
            return "";
        }

        std::string modif_name = name;

        // Mettre tout le nom en minuscules
        std::transform(
            modif_name.begin(),
            modif_name.end(),
            modif_name.begin(),
            [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            }
            );

        // Si on ne veut pas le pluriel, on retourne seulement le nom en minuscules
        if (!must_be_plural) {
            return modif_name;
        }

        // Déjà pluriel évident : categories, companies, policies
        if (modif_name.size() >= 3 && modif_name.ends_with("ies")) {
            return modif_name;
        }

        // Déjà pluriel simple : users, articles, tags
        // On évite address, class, status, etc.
        if (modif_name.ends_with('s') &&
            !modif_name.ends_with("ss") &&
            !modif_name.ends_with("us")) {
            return modif_name;
        }

        // category -> categories
        // company  -> companies
        if (modif_name.size() >= 2 && modif_name.back() == 'y') {
            char before_y = modif_name[modif_name.size() - 2];

            const bool before_is_vowel =
                before_y == 'a' ||
                before_y == 'e' ||
                before_y == 'i' ||
                before_y == 'o' ||
                before_y == 'u';

            if (!before_is_vowel) {
                modif_name.pop_back();
                modif_name += "ies";
                return modif_name;
            }
        }

        // address -> addresses
        // class   -> classes
        // box     -> boxes
        // church  -> churches
        // dish    -> dishes
        if (modif_name.ends_with("s") ||
            modif_name.ends_with("ss") ||
            modif_name.ends_with("x") ||
            modif_name.ends_with("z") ||
            modif_name.ends_with("ch") ||
            modif_name.ends_with("sh")) {
            return modif_name + "es";
        }

        return modif_name + "s";
    }

    [[nodiscard]] std::string route_prefix() const {
        if (name.empty()) {
            return "/";
        }
        // "User" → "/users"
        return "/" + to_route_plural(name);
    }

    [[nodiscard]] const Field* find_field(std::string_view n) const {
        auto it = std::find_if(fields.begin(), fields.end(),
                               [&](const Field& f) { return f.name == n; });

        if (it == fields.end()) {
            return nullptr;
        }

        return &(*it);
    }

    [[nodiscard]] bool has_field(std::string_view n) const {
        return find_field(n) != nullptr;
    }
    // Champs exposés dans les réponses JSON (ex: exclut password)
    [[nodiscard]] std::vector<Field> serializable_fields() const {
        std::vector<Field> out;
        for (const auto& f : fields)
            if (f.serializable) out.push_back(f);
        return out;
    }

    [[nodiscard]] bool has_seeds() const noexcept {
        return !seeds.empty();
    }

    // trouve une relation par nom
    [[nodiscard]] const Relation* find_relation(std::string_view n) const {
        auto it = std::find_if(relations.begin(), relations.end(),
                               [&](const Relation& r) { return r.name == n; });
        if (it == relations.end()) return nullptr;
        return &(*it);
    }

    [[nodiscard]] bool has_relation(std::string_view n) const {
        return find_relation(n) != nullptr;
    }

    // ── helpers pagination ─────────────────────────────────────

    [[nodiscard]] bool has_pagination() const noexcept {
        return pagination.has_value() && pagination->any();
    }
    [[nodiscard]] bool has_page_pagination() const noexcept {
        return pagination.has_value() && pagination->has_page();
    }
    [[nodiscard]] bool has_offset_pagination() const noexcept {
        return pagination.has_value() && pagination->has_offset();
    }
    [[nodiscard]] bool has_cursor_pagination() const noexcept {
        return pagination.has_value() && pagination->has_cursor();
    }

    // Indique si l'entite contient au moins un champ de type File.
    // C'est la responsabilite naturelle d'Entity (qui possede les Fields)
    // de savoir si elle expose un champ File. Schema et Service delegent
    // ici pour la convenance des appelants.
    //
    // Utilise par :
    //   - mysql_bootstrapper (via Schema::has_file_fields) : decider
    //     de creer ou non sea_files au boot
    //   - FileServiceFactory (via Service::has_file_fields) : decider
    //     d'instancier ou non le FileService
    //   - route_registration::register_file_download_routes : iterer
    //     uniquement sur les entites qui ont des fichiers
    [[nodiscard]] bool has_file_fields() const noexcept {
        for (const auto& f : fields) {
            if (f.is_file_field()) {
                return true;
            }
        }
        return false;
    }


};

} // namespace sea::domain

