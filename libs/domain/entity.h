#pragma once

#include "field.h"
#include "relation.h"

#include <string>
#include <vector>
#include <algorithm>

namespace sea::domain {

struct EntityOptions {
    bool enable_crud       = true;   // génère GET/POST/PUT/DELETE
    bool enable_auth       = false;  // génère /register /login /token
    bool enable_websocket  = false;  // génère ws://<entity>/live
    bool soft_delete       = false;  // ajoute deleted_at, ne supprime pas vraiment
    bool timestamps        = true;   // ajoute created_at / updated_at automatiquement
};

struct Entity {
    std::string            name;        // ex: "User"  (PascalCase)
    std::string            table_name;  // ex: "users" (calculé si vide)
    std::vector<Field>     fields;
    std::vector<Relation>  relations;
    EntityOptions          options;

    // ── helpers ────────────────────────────────────────────────

    [[nodiscard]] std::string route_prefix() const {
        if (name.empty()) {
            return "/";
        }
        // "User" → "/users"
        std::string s = name;
        s[0] = static_cast<char>(std::tolower(s[0]));
        return "/" + s + "s";   // MVP : pluriel naïf, à améliorer
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
};

} // namespace sea::domain

