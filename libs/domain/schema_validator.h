#pragma once

#include "schema.h"

#include <string>
#include <vector>

namespace sea::domain {

// ─────────────────────────────────────────────────────────────
// SchemaValidator
//
// Vérifie la cohérence métier d’un schéma avant de l’envoyer
// au runtime générique ou plus tard au code generator.
//
// Le validator retourne une liste d’erreurs.
// - liste vide   -> schéma valide
// - liste non vide -> schéma invalide
// ─────────────────────────────────────────────────────────────
class SchemaValidator {
public:
    // Point d’entrée principal
    [[nodiscard]] std::vector<std::string> validate(const Schema& schema) const;

private:
    // Validation globale du schéma
    void validate_entities(const Schema& schema,
                           std::vector<std::string>& errors) const;

    // Validation d’une entité individuelle
    void validate_entity(const Entity& entity,
                         const Schema& schema,
                         std::vector<std::string>& errors) const;

    // Validation des champs d’une entité
    void validate_fields(const Entity& entity,
                         std::vector<std::string>& errors) const;

    // Validation spécifique aux champs de type File.
    //
    // Vérifie :
    //   - cohérence type/config (file_config présent si type == File)
    //   - contraintes incompatibles (unique, indexed, max_length)
    //   - storage_path sûr (relatif, pas de '../', pas de caractères dangereux)
    //   - max_size_bytes > 0 et raisonnable (warning si très grand)
    //   - collisions de storage_path entre champs File de la même entité
    //
    // Appelée depuis validate_fields après les checks génériques. Centralise
    // toutes les règles "File" pour garder validate_fields lisible.
    void validate_file_field(const Entity& entity,
                             const Field& field,
                             std::vector<std::string>& errors) const;

    // Validation des relations d’une entité
    void validate_relations(const Entity& entity,
                            const Schema& schema,
                            std::vector<std::string>& errors) const;

    // Validation de la pagination d’une entité (3 modes possibles)
    void validate_pagination(const Entity& entity,
                             std::vector<std::string>& errors) const;

    // Vérifie qu'une chaîne "field:direction" est correcte et que `field`
    // est dans la whitelist `allowed`. Retourne une erreur descriptive,
    // ou std::nullopt si tout va bien.
    [[nodiscard]] std::optional<std::string>
    validate_sort_expression(const std::string& expression,
                             const std::vector<std::string>& allowed,
                             const Entity& entity,
                             const std::string& context) const;


    // Helpers de validation
    [[nodiscard]] bool is_blank(std::string_view value) const noexcept;
    [[nodiscard]] bool is_valid_identifier(std::string_view value) const noexcept;
};

} // namespace sea::domain