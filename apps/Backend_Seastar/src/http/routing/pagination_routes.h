#pragma once

#include "route_registration.h"
#include "route_generator.h"

#include <seastar/http/httpd.hh>

#include <memory>
#include <vector>

namespace sea::infrastructure::runtime {
class GenericCrudEngine;
}

namespace sea::http::routing {

// ─────────────────────────────────────────────────────────────────────
// register_pagination_routes
//
// Enregistre dans le routeur Seastar toutes les routes paginees
// declarees dans `route_definitions`, c'est-a-dire celles dont
// l'operation_name se termine par "_page", "_offset" ou "_cursor".
//
// Les routes paginees sont generees par route_generator (etape 3)
// uniquement pour les entites ayant un bloc pagination: dans le YAML.
// Si aucune route paginee n'est dans `route_definitions`, cette
// fonction ne fait rien.
//
// Particularite Seastar : les paths comme /users/.../{id}/page ne sont
// pas exprimables avec url().remainder() (qui doit etre en fin).
// Cette fonction utilise donc l'API match_rule de Seastar
// (cf paginated_match_rule.h) qui permet de composer
// statique+param+statique.
//
// Le contrat d'autorisation des routes paginees est identique a leur
// route source (un /users/page utilise les memes checks qu'un /users).
// Cela est gere par RouteAuthorizationResolver::strip_pagination_suffix
// (etape 3, deja en place).
//
// Parametres :
// - routes            : objet Seastar a remplir
// - route_definitions : sortie de RouteGenerator (toutes les routes,
//                       paginees ou non ; on filtre ici)
// - crud_engine       : pour appeler list_page/offset/cursor
// - context           : middlewares, schema, auth, etc.
// ─────────────────────────────────────────────────────────────────────

void register_pagination_routes(
    seastar::httpd::routes& routes,
    const std::vector<sea::application::RouteDefinition>& route_definitions,
    const std::shared_ptr<sea::infrastructure::runtime::GenericCrudEngine>& crud_engine,
    const MiddlewareContext& context
    );

} // namespace sea::http::routing