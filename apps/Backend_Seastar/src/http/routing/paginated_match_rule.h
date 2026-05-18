#pragma once

#include <seastar/http/matchrules.hh>

#include <string>

namespace sea::http::routing {

// ─────────────────────────────────────────────────────────────────────
// build_match_rule_from_template
//
// Construit un seastar::httpd::match_rule a partir d'un path template
// avec des placeholders nommes type "{id}" ou "{value}".
//
// Exemples :
//   "/users/page"
//     -> add_str("/users/page")
//
//   "/users/filter/with_department/{id}/offset"
//     -> add_str("/users/filter/with_department/")
//        add_param("id")
//        add_str("/offset")
//
//   "/departments_with_users/{id}/cursor"
//     -> add_str("/departments_with_users/")
//        add_param("id")
//        add_str("/cursor")
//
// Le match_rule retourne (cree au new) prend ownership du handler passe.
// L'appelant doit ensuite l'enregistrer via routes.add(rule, method) qui
// prend ownership du match_rule.
//
// Pourquoi ce helper :
// - L'API match_rule de Seastar fonctionne en composant statique+param
// - On a 15 routes paginees a enregistrer, faire le parsing du template
//   a la main 15 fois serait penible et bug-prone
// - Avec ce helper, on passe le template tel quel + le handler, c'est tout
// ─────────────────────────────────────────────────────────────────────

seastar::httpd::match_rule* build_match_rule_from_template(
    const std::string& path_template,
    seastar::httpd::handler_base* handler
    );

} // namespace sea::http::routing