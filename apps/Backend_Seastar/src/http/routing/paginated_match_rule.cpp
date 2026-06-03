#include "paginated_match_rule.h"

#include <stdexcept>

namespace sea::http::routing {

seastar::httpd::match_rule* build_match_rule_from_template(
    const std::string& path_template,
    seastar::httpd::handler_base* handler)
{
    auto* rule = new seastar::httpd::match_rule(handler);

    // On scanne le template caractere par caractere. Chaque fois qu'on
    // rencontre un '{', on emet le buffer statique accumule (s'il y en a)
    // puis on lit le nom du parametre jusqu'au '}'.
    //
    // FIX (juin 2026) : retirer le slash final du buffer avant d'emettre
    // l'add_str qui precede un add_param. Raison : seastar::httpd::str_matcher
    // exige qu'apres sa zone matchee, on soit soit a la fin de l'URL, soit
    // sur un '/'. Donc add_str("/users/") sur url "/users/abc" echoue parce
    // que apres "/users/" on a 'a' (pas '/' ni fin). En retirant le slash
    // final du buffer, le param_matcher consommera ce slash lui-meme
    // (cf. find_end_param qui inclut le slash dans la valeur du parametre).
    //
    // Consequence : le param_matcher retourne une valeur AVEC slash de tete
    // ("/abc" au lieu de "abc"). Les handlers qui lisent get_path_param
    // doivent retirer ce slash de tete avant d'utiliser la valeur.
    //
    // Le buffer statique restant en fin de boucle est emis tel quel.
    //
    // Exemple : "/users/filter/with_dept/{id}/page"
    //   - accumule "/users/filter/with_dept/"
    //   - rencontre '{' -> retire le '/' final -> add_str("/users/filter/with_dept")
    //   - lit "id", add_param("id") -> consommera "/<id>"
    //   - accumule "/page"
    //   - fin -> add_str("/page")
    std::string buffer;
    std::size_t i = 0;
    const std::size_t n = path_template.size();

    while (i < n) {
        const char c = path_template[i];

        if (c == '{') {
            // FIX : retirer le slash final du buffer pour que str_matcher
            // ne le matche pas (sinon il echoue au caractere apres son
            // pattern). Le param_matcher l'absorbera.
            if (!buffer.empty() && buffer.back() == '/') {
                buffer.pop_back();
            }

            // Flush du buffer statique accumule
            if (!buffer.empty()) {
                rule->add_str(seastar::sstring(buffer));
                buffer.clear();
            }

            // Lit le nom du parametre
            const std::size_t close = path_template.find('}', i);
            if (close == std::string::npos) {
                delete rule;
                throw std::runtime_error(
                    "build_match_rule_from_template: '{' sans '}' correspondant dans '"
                    + path_template + "'"
                    );
            }

            const std::string param_name = path_template.substr(i + 1, close - i - 1);
            if (param_name.empty()) {
                delete rule;
                throw std::runtime_error(
                    "build_match_rule_from_template: parametre vide '{}' dans '"
                    + path_template + "'"
                    );
            }

            rule->add_param(seastar::sstring(param_name));
            i = close + 1;
        } else {
            buffer.push_back(c);
            ++i;
        }
    }

    // Flush final
    if (!buffer.empty()) {
        rule->add_str(seastar::sstring(buffer));
    }

    return rule;
}

} // namespace sea::http::routing