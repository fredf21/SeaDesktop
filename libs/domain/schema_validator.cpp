#include "schema_validator.h"

#include <cctype>
#include <string_view>
#include <unordered_set>

namespace sea::domain {

std::vector<std::string> SchemaValidator::validate(const Schema& schema) const {
    std::vector<std::string> errors;

    // Un schéma vide n’a pas de sens pour ton MVP
    if (schema.empty()) {
        errors.push_back("Le schema ne contient aucune entite.");
        return errors;
    }

    validate_entities(schema, errors);

    return errors;
}

void SchemaValidator::validate_entities(const Schema& schema,
                                        std::vector<std::string>& errors) const {
    std::unordered_set<std::string> entity_names;

    for (const auto& entity : schema.entities) {
        // Nom d'entité obligatoire
        if (is_blank(entity.name)) {
            errors.push_back("Une entite a un nom vide.");
            continue;
        }

        // Vérifie un identifiant raisonnable
        if (!is_valid_identifier(entity.name)) {
            errors.push_back("Nom d'entite invalide: '" + entity.name + "'.");
        }

        // Noms d'entités uniques
        if (!entity_names.insert(entity.name).second) {
            errors.push_back("Nom d'entite duplique: '" + entity.name + "'.");
        }

        validate_entity(entity, schema, errors);
    }
}

void SchemaValidator::validate_entity(const Entity& entity,
                                      const Schema& schema,
                                      std::vector<std::string>& errors) const {
    // Pour le MVP, une entité sans champ est considérée invalide
    if (entity.fields.empty()) {
        errors.push_back("L'entite '" + entity.name + "' ne contient aucun champ.");
    }

    validate_fields(entity, errors);
    validate_relations(entity, schema, errors);
}

void SchemaValidator::validate_fields(const Entity& entity,
                                      std::vector<std::string>& errors) const {
    std::unordered_set<std::string> field_names;

    for (const auto& field : entity.fields) {
        if (is_blank(field.name)) {
            errors.push_back("L'entite '" + entity.name + "' contient un champ sans nom.");
            continue;
        }

        if (!is_valid_identifier(field.name)) {
            errors.push_back("Nom de champ invalide '" + field.name +
                             "' dans l'entite '" + entity.name + "'.");
        }

        if (!field_names.insert(field.name).second) {
            errors.push_back("Champ duplique '" + field.name +
                             "' dans l'entite '" + entity.name + "'.");
        }

        // Password ne doit jamais être sérialisable par défaut
        if (field.type == FieldType::Password && field.serializable) {
            errors.push_back("Le champ password '" + field.name +
                             "' dans l'entite '" + entity.name +
                             "' ne devrait pas etre serializable.");
        }

        // Un champ Password ne devrait pas avoir de valeur par défaut
        if (field.type == FieldType::Password && field.has_default()) {
            errors.push_back("Le champ password '" + field.name +
                             "' dans l'entite '" + entity.name +
                             "' ne peut pas avoir de valeur par defaut.");
        }

        // max_length doit rester réservé aux String/Text
        if (field.max_length.has_value()) {
            if (field.type != FieldType::String && field.type != FieldType::Text) {
                errors.push_back("Le champ '" + field.name + "' dans l'entite '" +
                                 entity.name +
                                 "' utilise max_length avec un type incompatible.");
            }

            if (*field.max_length == 0) {
                errors.push_back("Le champ '" + field.name + "' dans l'entite '" +
                                 entity.name +
                                 "' ne peut pas avoir max_length = 0.");
            }
        }

        // min/max cohérents
        if (field.min_value.has_value() && field.max_value.has_value()) {
            // comparaison uniquement si les deux variantes ont le même type
            if (field.min_value->index() == field.max_value->index()) {
                if (std::holds_alternative<int64_t>(*field.min_value)) {
                    const auto min_v = std::get<int64_t>(*field.min_value);
                    const auto max_v = std::get<int64_t>(*field.max_value);

                    if (min_v > max_v) {
                        errors.push_back("Le champ '" + field.name + "' dans l'entite '" +
                                         entity.name +
                                         "' a min_value > max_value.");
                    }
                } else if (std::holds_alternative<double>(*field.min_value)) {
                    const auto min_v = std::get<double>(*field.min_value);
                    const auto max_v = std::get<double>(*field.max_value);

                    if (min_v > max_v) {
                        errors.push_back("Le champ '" + field.name + "' dans l'entite '" +
                                         entity.name +
                                         "' a min_value > max_value.");
                    }
                }
            }
        }
    }
}

void SchemaValidator::validate_relations(const Entity& entity,
                                         const Schema& schema,
                                         std::vector<std::string>& errors) const {
    std::unordered_set<std::string> relation_names;

    for (const auto& relation : entity.relations) {
        if (is_blank(relation.name)) {
            errors.push_back("L'entite '" + entity.name + "' contient une relation sans nom.");
            continue;
        }

        if (!relation_names.insert(relation.name).second) {
            errors.push_back("Relation dupliquee '" + relation.name +
                             "' dans l'entite '" + entity.name + "'.");
        }

        if (is_blank(relation.target_entity)) {
            errors.push_back("La relation '" + relation.name + "' dans l'entite '" +
                             entity.name + "' n'a pas de target_entity.");
            continue;
        }

        // La cible doit exister dans le schéma
        if (!schema.has_entity(relation.target_entity)) {
            errors.push_back("La relation '" + relation.name + "' dans l'entite '" +
                             entity.name + "' cible une entite inconnue: '" +
                             relation.target_entity + "'.");
        }

        // ManyToMany doit avoir une table pivot si elle est renseignée explicitement
        if (relation.kind == RelationKind::ManyToMany) {
            if (relation.pivot_table.empty()) {
                // Pour le MVP, on peut tolérer l'absence et laisser le générateur
                // la calculer plus tard. Donc pas d'erreur bloquante ici.
            }
        }

        // BelongsTo a souvent besoin d'une fk locale
        if (relation.kind == RelationKind::BelongsTo) {
            if (relation.fk_column.empty()) {
                // Là aussi, on peut le laisser vide pour calcul automatique plus tard.
            }
        }
    }
}

bool SchemaValidator::is_blank(std::string_view value) const noexcept {
    return value.empty();
}

bool SchemaValidator::is_valid_identifier(std::string_view value) const noexcept {
    if (value.empty()) {
        return false;
    }

    const unsigned char first = static_cast<unsigned char>(value.front());

    // Premier caractère : lettre ou underscore
    if (!(std::isalpha(first) || first == '_')) {
        return false;
    }

    // Le reste : lettre, chiffre ou underscore
    for (char c : value) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (!(std::isalnum(uc) || uc == '_')) {
            return false;
        }
    }

    return true;
}

} // namespace sea::domain
