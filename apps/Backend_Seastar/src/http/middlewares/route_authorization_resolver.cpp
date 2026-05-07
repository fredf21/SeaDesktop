#include "route_authorization_resolver.h"

#include "entity.h"
#include "relation.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace sea::http::middlewares {

namespace {

using sea::domain::access_control::CrudOperation;

// ──────────────────────────────────────────────────────────────────
// Helpers strings
// ──────────────────────────────────────────────────────────────────

std::string to_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

bool iequals(std::string_view a, std::string_view b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

bool starts_with(std::string_view s, std::string_view prefix)
{
    return s.size() >= prefix.size()
    && s.compare(0, prefix.size(), prefix) == 0;
}

// Compte le nombre de '/' dans une chaine
std::size_t count_slashes(std::string_view s)
{
    return std::count(s.begin(), s.end(), '/');
}

// "User" → "user"
std::string entity_to_lower(const std::string& entity_name)
{
    std::string s = entity_name;
    if (!s.empty()) {
        s[0] = static_cast<char>(std::tolower(s[0]));
    }
    return s;
}

// "User" → "/users"
std::string entity_collection_path(const std::string& entity_name)
{
    return "/" + entity_to_lower(entity_name) + "s";
}

// Trouve l'entite parent par le nom de la FK (ex: "department_id" → Department)
const sea::domain::Entity* find_entity_by_fk_name(
    const sea::domain::Schema& schema,
    const std::string& fk_basename)
{
    // fk_basename est "department" (sans le _id)
    // On cherche une entite "Department" (capitalize first)
    std::string entity_guess = fk_basename;
    if (!entity_guess.empty()) {
        entity_guess[0] = static_cast<char>(std::toupper(entity_guess[0]));
    }
    return schema.find_entity(entity_guess);
}

} // namespace anonyme

// ──────────────────────────────────────────────────────────────────
// RouteAuthorizationResolver
// ──────────────────────────────────────────────────────────────────

RouteAuthorizationResolver::RouteAuthorizationResolver(
    const sea::domain::Schema& schema)
    : schema_(schema)
{
}

RouteAuthorizationPlan RouteAuthorizationResolver::resolve(
    const std::string& method,
    const std::string& path) const
{
    // Essai dans l'ordre du plus specifique au moins specifique :
    // 1. Filter by parent (avec FK ou field)
    if (auto plan = try_match_filter_by_parent(method, path); plan.has_value()) {
        return *plan;
    }

    if (auto plan = try_match_filter_by_parent_field(method, path); plan.has_value()) {
        return *plan;
    }

    // 2. Parent with children : /<parent>s_with_<children>/{id}
    if (auto plan = try_match_parent_with_children(method, path); plan.has_value()) {
        return *plan;
    }

    // 3. Item relation : /<entity>s/{id}/<relation>
    if (auto plan = try_match_item_relation(method, path); plan.has_value()) {
        return *plan;
    }

    // 4. CRUD standard : /<entity>s, /<entity>s/{id}
    if (auto plan = try_match_crud(method, path); plan.has_value()) {
        return *plan;
    }

    // Aucun match → unknown route (le middleware decide fail-closed)
    return RouteAuthorizationPlan{ {}, true };
}

// ──────────────────────────────────────────────────────────────────
// Pattern 1 : CRUD standard
// /<entity>s         → list (GET) / create (POST)
// /<entity>s/{id}    → get_by_id (GET) / update (PUT) / delete (DELETE)
// ──────────────────────────────────────────────────────────────────

std::optional<RouteAuthorizationPlan>
RouteAuthorizationResolver::try_match_crud(
    const std::string& method,
    const std::string& path) const
{
    for (const auto& entity : schema_.entities) {
        const std::string base = entity_collection_path(entity.name);

        // Match exact : /<entity>s
        if (path == base) {
            if (iequals(method, "GET")) {
                return RouteAuthorizationPlan{
                    { AuthorizationCheck{
                        entity.name,
                        CrudOperation::List,
                        "list " + entity.name
                    } },
                    false
                };
            }
            if (iequals(method, "POST")) {
                return RouteAuthorizationPlan{
                    { AuthorizationCheck{
                        entity.name,
                        CrudOperation::Create,
                        "create " + entity.name
                    } },
                    false
                };
            }
            // Autre methode sur /<entity>s → unknown
            continue;
        }

        // Match item : /<entity>s/{id}
        const std::string item_prefix = base + "/";
        if (starts_with(path, item_prefix)) {
            const std::string remainder = path.substr(item_prefix.size());

            // Si remainder contient "/", c'est une sous-route → pas un CRUD standard
            if (remainder.find('/') != std::string::npos) {
                continue;
            }

            // Si remainder est vide ou contient "filter", c'est pas un id → ignore
            if (remainder.empty() || remainder == "filter") {
                continue;
            }

            if (iequals(method, "GET")) {
                return RouteAuthorizationPlan{
                    { AuthorizationCheck{
                        entity.name,
                        CrudOperation::GetById,
                        "get_by_id " + entity.name
                    } },
                    false
                };
            }
            if (iequals(method, "PUT") || iequals(method, "PATCH")) {
                return RouteAuthorizationPlan{
                    { AuthorizationCheck{
                        entity.name,
                        CrudOperation::Update,
                        "update " + entity.name
                    } },
                    false
                };
            }
            if (iequals(method, "DELETE")) {
                return RouteAuthorizationPlan{
                    { AuthorizationCheck{
                        entity.name,
                        CrudOperation::Delete,
                        "delete " + entity.name
                    } },
                    false
                };
            }
        }
    }

    return std::nullopt;
}

// ──────────────────────────────────────────────────────────────────
// Pattern 2 : Filter by parent FK
// GET /<entity>s/filter/with_<parent>/{id}
// → Strategie C : check Parent.get_by_id + Entity.list
// ──────────────────────────────────────────────────────────────────

std::optional<RouteAuthorizationPlan>
RouteAuthorizationResolver::try_match_filter_by_parent(
    const std::string& method,
    const std::string& path) const
{
    if (!iequals(method, "GET")) {
        return std::nullopt;
    }

    // Pattern : /<entity>s/filter/with_<parent>/{id}
    // Doit avoir exactement 5 segments : "", entity+s, "filter", "with_X", id
    // → 4 slashes
    if (count_slashes(path) != 4) {
        return std::nullopt;
    }

    for (const auto& entity : schema_.entities) {
        const std::string filter_prefix =
            entity_collection_path(entity.name) + "/filter/with_";

        if (!starts_with(path, filter_prefix)) {
            continue;
        }

        // Extrait la partie apres "with_"
        const std::string after_with = path.substr(filter_prefix.size());

        // Doit etre format "<parent>/{id}"
        const auto slash_pos = after_with.find('/');
        if (slash_pos == std::string::npos) {
            continue;
        }

        const std::string parent_part = after_with.substr(0, slash_pos);

        // parent_part peut etre "department" ou "department_name"
        // Pour le filter_by_parent_FK, on attend juste "department"
        // Si ca contient un "_", c'est probablement filter_by_parent_field
        if (parent_part.find('_') != std::string::npos) {
            // Sera traite par try_match_filter_by_parent_field
            continue;
        }

        // Trouve l'entite parent
        const auto* parent_entity = find_entity_by_fk_name(schema_, parent_part);
        if (parent_entity == nullptr) {
            continue;
        }

        // Match : Strategie C double check
        return RouteAuthorizationPlan{
            {
                AuthorizationCheck{
                    parent_entity->name,
                    CrudOperation::GetById,
                    "parent " + parent_entity->name + " (filter)"
                },
                AuthorizationCheck{
                    entity.name,
                    CrudOperation::List,
                    "child " + entity.name + " (list)"
                }
            },
            false
        };
    }

    return std::nullopt;
}

// ──────────────────────────────────────────────────────────────────
// Pattern 3 : Filter by parent field
// GET /<entity>s/filter/with_<parent>_<field>/{value}
// → Strategie C : check Parent.list + Entity.list
// (on prend Parent.list car on filtre sur un champ, pas un id)
// ──────────────────────────────────────────────────────────────────

std::optional<RouteAuthorizationPlan>
RouteAuthorizationResolver::try_match_filter_by_parent_field(
    const std::string& method,
    const std::string& path) const
{
    if (!iequals(method, "GET")) {
        return std::nullopt;
    }

    if (count_slashes(path) != 4) {
        return std::nullopt;
    }

    for (const auto& entity : schema_.entities) {
        const std::string filter_prefix =
            entity_collection_path(entity.name) + "/filter/with_";

        if (!starts_with(path, filter_prefix)) {
            continue;
        }

        const std::string after_with = path.substr(filter_prefix.size());
        const auto slash_pos = after_with.find('/');
        if (slash_pos == std::string::npos) {
            continue;
        }

        const std::string parent_and_field = after_with.substr(0, slash_pos);

        // Doit avoir un "_" pour separer parent du field
        const auto underscore_pos = parent_and_field.find('_');
        if (underscore_pos == std::string::npos) {
            // Sans underscore, c'est filter_by_parent_FK (deja traite)
            continue;
        }

        const std::string parent_part = parent_and_field.substr(0, underscore_pos);

        const auto* parent_entity = find_entity_by_fk_name(schema_, parent_part);
        if (parent_entity == nullptr) {
            continue;
        }

        // Match : Strategie C double check (avec Parent.list car filter sur field)
        return RouteAuthorizationPlan{
            {
                AuthorizationCheck{
                    parent_entity->name,
                    CrudOperation::List,
                    "parent " + parent_entity->name + " (filter by field)"
                },
                AuthorizationCheck{
                    entity.name,
                    CrudOperation::List,
                    "child " + entity.name + " (list)"
                }
            },
            false
        };
    }

    return std::nullopt;
}

// ──────────────────────────────────────────────────────────────────
// Pattern 4 : Item relation
// GET /<entity>s/{id}/<relation>
// → Strategie C : check Entity.get_by_id + Target.list
// ──────────────────────────────────────────────────────────────────

std::optional<RouteAuthorizationPlan>
RouteAuthorizationResolver::try_match_item_relation(
    const std::string& method,
    const std::string& path) const
{
    if (!iequals(method, "GET")) {
        return std::nullopt;
    }

    // Doit avoir 3 slashes : "", entity+s, id, relation
    if (count_slashes(path) != 3) {
        return std::nullopt;
    }

    for (const auto& entity : schema_.entities) {
        const std::string base = entity_collection_path(entity.name);
        const std::string prefix = base + "/";

        if (!starts_with(path, prefix)) {
            continue;
        }

        const std::string remainder = path.substr(prefix.size());
        const auto slash_pos = remainder.find('/');
        if (slash_pos == std::string::npos) {
            continue;
        }

        const std::string id_part = remainder.substr(0, slash_pos);
        const std::string relation_part = remainder.substr(slash_pos + 1);

        if (id_part.empty() || relation_part.empty()) {
            continue;
        }

        // "filter" est pris par d'autres patterns
        if (id_part == "filter" || relation_part == "filter") {
            continue;
        }

        // Cherche la relation dans l'entite source
        const sea::domain::Relation* matching_rel = nullptr;
        for (const auto& rel : entity.relations) {
            if (rel.name == relation_part) {
                matching_rel = &rel;
                break;
            }
        }

        if (matching_rel == nullptr) {
            continue;
        }

        // Trouve l'entite cible
        const auto* target_entity = schema_.find_entity(matching_rel->target_entity);
        if (target_entity == nullptr) {
            continue;
        }

        // Match : Strategie C double check
        return RouteAuthorizationPlan{
            {
                AuthorizationCheck{
                    entity.name,
                    CrudOperation::GetById,
                    "parent " + entity.name + " (item)"
                },
                AuthorizationCheck{
                    target_entity->name,
                    CrudOperation::List,
                    "related " + target_entity->name + " (" + relation_part + ")"
                }
            },
            false
        };
    }

    return std::nullopt;
}

// ──────────────────────────────────────────────────────────────────
// Pattern 5 : Parent with children
// GET /<parent>s_with_<children>/{id}
// → Strategie C : check Parent.get_by_id + Children.list
// ──────────────────────────────────────────────────────────────────

std::optional<RouteAuthorizationPlan>
RouteAuthorizationResolver::try_match_parent_with_children(
    const std::string& method,
    const std::string& path) const
{
    if (!iequals(method, "GET")) {
        return std::nullopt;
    }

    // Doit avoir 2 slashes : "", parents_with_children, id
    if (count_slashes(path) != 2) {
        return std::nullopt;
    }

    // Verifie le pattern "_with_"
    const auto with_pos = path.find("_with_");
    if (with_pos == std::string::npos) {
        return std::nullopt;
    }

    // Verifie qu'il commence par "/"
    if (path.empty() || path[0] != '/') {
        return std::nullopt;
    }

    // Trouve la position du dernier "/"
    const auto last_slash = path.rfind('/');
    if (last_slash == std::string::npos || last_slash == 0) {
        return std::nullopt;
    }

    // Format attendu : /<parents>_with_<children>/{id}
    // Donc with_pos doit etre dans la 1ere partie (entre [1, last_slash])
    if (with_pos <= 1 || with_pos >= last_slash) {
        return std::nullopt;
    }

    // Extrait la 1ere partie : "<parents>_with_<children>"
    const std::string first_segment = path.substr(1, last_slash - 1);

    // Re-localise "_with_" dans cette sous-string
    const auto local_with = first_segment.find("_with_");
    if (local_with == std::string::npos) {
        return std::nullopt;
    }

    std::string parents_part  = first_segment.substr(0, local_with);
    std::string children_part = first_segment.substr(local_with + 6);

    // Enleve le "s" final pour reconstruire le nom d'entite
    if (!parents_part.empty() && parents_part.back() == 's') {
        parents_part.pop_back();
    }
    if (!children_part.empty() && children_part.back() == 's') {
        children_part.pop_back();
    }

    // Trouve les entites
    const auto* parent_entity  = find_entity_by_fk_name(schema_, parents_part);
    const auto* children_entity = find_entity_by_fk_name(schema_, children_part);

    if (parent_entity == nullptr || children_entity == nullptr) {
        return std::nullopt;
    }

    // Match : Strategie C double check
    return RouteAuthorizationPlan{
        {
            AuthorizationCheck{
                parent_entity->name,
                CrudOperation::GetById,
                "parent " + parent_entity->name + " (with children)"
            },
            AuthorizationCheck{
                children_entity->name,
                CrudOperation::List,
                "children " + children_entity->name + " (with parent)"
            }
        },
        false
    };
}

} // namespace sea::http::middlewares
