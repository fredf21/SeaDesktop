#pragma once

#include "entity.h"

#include <algorithm>
#include <string_view>
#include <vector>

namespace sea::domain {

// ─────────────────────────────────────────────────────────────
// Schema
//
// Représente l’ensemble des entités d’un service.
// Exemple :
//
// Service "UserService"
//   └── Schema
//         ├── User
//         ├── Role
//         └── Permission
// ─────────────────────────────────────────────────────────────
struct Schema {
    std::vector<Entity> entities;

    // ── helpers ─────────────────────────────────────────────

    // Trouve une entité par son nom (ex: "User")
    // Retourne nullptr si non trouvée
    [[nodiscard]] const Entity* find_entity(std::string_view name) const {
        auto it = std::find_if(entities.begin(), entities.end(),
                               [&](const Entity& e) {
                                   return e.name == name;
                               });

        if (it == entities.end()) {
            return nullptr;
        }

        return &(*it);
    }

    // Vérifie si une entité existe
    [[nodiscard]] bool has_entity(std::string_view name) const {
        return find_entity(name) != nullptr;
    }

    // Retourne toutes les entités exposant du CRUD
    [[nodiscard]] std::vector<const Entity*> crud_entities() const {
        std::vector<const Entity*> out;

        for (const auto& e : entities) {
            if (e.options.enable_crud) {
                out.push_back(&e);
            }
        }

        return out;
    }

    // Retourne toutes les entités avec auth activée
    [[nodiscard]] std::vector<const Entity*> auth_entities() const {
        std::vector<const Entity*> out;

        for (const auto& e : entities) {
            if (e.options.is_auth_source) {
                out.push_back(&e);
            }
        }

        return out;
    }

    // Vérifie si le schéma est vide
    [[nodiscard]] bool empty() const noexcept {
        return entities.empty();
    }
    // Indique si le schema utilise au moins un champ File.
    // Delegue a Entity::has_file_fields qui est la vraie responsabilite
    // (Entity contient les Fields, c'est elle qui peut introspecter).
    //
    // Helper de convenance pour les callers qui n'ont que le Schema
    // sous la main (ex: mysql_bootstrapper qui ne voit pas le Service).
    [[nodiscard]] bool has_file_fields() const noexcept {
        return std::any_of(
            entities.begin(), entities.end(),
            [](const Entity& e) { return e.has_file_fields(); });
    }

};

} // namespace sea::domain