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

    // Validation des relations d’une entité
    void validate_relations(const Entity& entity,
                            const Schema& schema,
                            std::vector<std::string>& errors) const;

    // Helpers de validation
    [[nodiscard]] bool is_blank(std::string_view value) const noexcept;
    [[nodiscard]] bool is_valid_identifier(std::string_view value) const noexcept;
};

} // namespace sea::domain