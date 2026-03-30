#pragma once

#include "service.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace sea::domain {

// ─────────────────────────────────────────────────────────────
// Project
//
// Représente un projet complet SeaDesktop.
// Contient un ensemble de services.
//
// Exemple :
//   Project "ECommerce"
//     ├── UserService
//     ├── ProductService
//     └── OrderService
// ─────────────────────────────────────────────────────────────
struct Project {
    std::string        name;      // ex: "SeaDesktopDemo"
    std::vector<Service> services;

    // ── helpers ─────────────────────────────────────────────

    // Trouve un service par son nom
    [[nodiscard]] const Service* find_service(std::string_view service_name) const {
        auto it = std::find_if(services.begin(), services.end(),
                               [&](const Service& s) {
                                   return s.name == service_name;
                               });

        if (it == services.end()) {
            return nullptr;
        }

        return &(*it);
    }

    // Vérifie si un service existe
    [[nodiscard]] bool has_service(std::string_view service_name) const {
        return find_service(service_name) != nullptr;
    }

    // Vérifie si le projet contient au moins un service
    [[nodiscard]] bool empty() const noexcept {
        return services.empty();
    }

    // Retourne tous les services utilisant une DB mémoire
    [[nodiscard]] std::vector<const Service*> memory_services() const {
        std::vector<const Service*> out;

        for (const auto& s : services) {
            if (s.uses_memory_database()) {
                out.push_back(&s);
            }
        }

        return out;
    }

    // Retourne tous les services utilisant une DB externe
    [[nodiscard]] std::vector<const Service*> external_db_services() const {
        std::vector<const Service*> out;

        for (const auto& s : services) {
            if (s.uses_external_database()) {
                out.push_back(&s);
            }
        }

        return out;
    }
};

} // namespace sea::domain