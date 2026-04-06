#include "generic_validator.h"

#include <regex>
#include <variant>

namespace sea::infrastructure::runtime {

bool GenericValidator::matches_type(sea::domain::FieldType type,
                                    const DynamicValue& value) const {
    switch (type) {
    case sea::domain::FieldType::String:
    case sea::domain::FieldType::Text:
    case sea::domain::FieldType::UUID:
    case sea::domain::FieldType::Password:
    case sea::domain::FieldType::Email:
    case sea::domain::FieldType::Timestamp:
        return std::holds_alternative<std::string>(value);

    case sea::domain::FieldType::Int:
        return std::holds_alternative<std::int64_t>(value);

    case sea::domain::FieldType::Float:
        return std::holds_alternative<double>(value);

    case sea::domain::FieldType::Bool:
        return std::holds_alternative<bool>(value);
    }

    return false;
}

bool GenericValidator::is_valid_email(const std::string& value) const {
    // MVP : validation simple
    static const std::regex email_regex(
        R"(^[A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,}$)"
        );

    return std::regex_match(value, email_regex);
}

std::vector<std::string>
GenericValidator::validate(const sea::domain::Entity& entity,
                           const DynamicRecord& record) const {
    std::vector<std::string> errors;

    for (const auto& field : entity.fields) {
        // Le champ id est géré automatiquement par le backend
        if (field.name == "id") continue;

        const auto it = record.find(field.name);

        // Champ absent
        if (it == record.end()) {
            if (field.required && !field.has_default()) {
                errors.push_back("Champ requis manquant: " + field.name);
            }
            continue;
        }

        const auto& value = it->second;

        // null logique / monostate
        if (std::holds_alternative<std::monostate>(value)) {
            if (field.required) {
                errors.push_back("Champ requis null: " + field.name);
            }
            continue;
        }

        // Vérification du type
        if (!matches_type(field.type, value)) {
            errors.push_back("Type invalide pour le champ: " + field.name);
            continue;
        }

        // Validation Email
        if (field.type == sea::domain::FieldType::Email) {
            const auto& email = std::get<std::string>(value);
            if (!is_valid_email(email)) {
                errors.push_back("Format email invalide pour le champ: " + field.name);
            }
        }

        // Validation max_length
        if (field.max_length.has_value()) {
            if (std::holds_alternative<std::string>(value)) {
                const auto& s = std::get<std::string>(value);
                if (s.size() > *field.max_length) {
                    errors.push_back("Champ trop long: " + field.name);
                }
            }
        }

        // Validation min/max
        if (field.min_value.has_value()) {
            if (std::holds_alternative<std::int64_t>(value) &&
                std::holds_alternative<std::int64_t>(*field.min_value)) {
                if (std::get<std::int64_t>(value) < std::get<std::int64_t>(*field.min_value)) {
                    errors.push_back("Champ inferieur au minimum: " + field.name);
                }
            } else if (std::holds_alternative<double>(value) &&
                       std::holds_alternative<double>(*field.min_value)) {
                if (std::get<double>(value) < std::get<double>(*field.min_value)) {
                    errors.push_back("Champ inferieur au minimum: " + field.name);
                }
            }
        }

        if (field.max_value.has_value()) {
            if (std::holds_alternative<std::int64_t>(value) &&
                std::holds_alternative<std::int64_t>(*field.max_value)) {
                if (std::get<std::int64_t>(value) > std::get<std::int64_t>(*field.max_value)) {
                    errors.push_back("Champ superieur au maximum: " + field.name);
                }
            } else if (std::holds_alternative<double>(value) &&
                       std::holds_alternative<double>(*field.max_value)) {
                if (std::get<double>(value) > std::get<double>(*field.max_value)) {
                    errors.push_back("Champ superieur au maximum: " + field.name);
                }
            }
        }
    }

    return errors;
}

std::vector<std::string> GenericValidator::validate_partial(const domain::Entity &entity, const DynamicRecord &record) const
{
    std::vector<std::string> errors;
    for (const auto& field : entity.fields) {
        // Le champ id est géré automatiquement par le backend
        if (field.name == "id") continue;

        const auto it = record.find(field.name);

        // Champ absent — pas d'erreur pour le update partiel
        if (it == record.end()) continue;

        const auto& value = it->second;

        // null logique / monostate
        if (std::holds_alternative<std::monostate>(value)) {
            if (field.required) {
                errors.push_back("Champ requis null: " + field.name);
            }
            continue;
        }

        // Vérification du type
        if (!matches_type(field.type, value)) {
            errors.push_back("Type invalide pour le champ: " + field.name);
            continue;
        }

        // Validation Email
        if (field.type == sea::domain::FieldType::Email) {
            const auto& email = std::get<std::string>(value);
            if (!is_valid_email(email)) {
                errors.push_back("Format email invalide pour le champ: " + field.name);
            }
        }

        // Validation max_length
        if (field.max_length.has_value()) {
            if (std::holds_alternative<std::string>(value)) {
                const auto& s = std::get<std::string>(value);
                if (s.size() > *field.max_length) {
                    errors.push_back("Champ trop long: " + field.name);
                }
            }
        }

        // Validation min/max
        if (field.min_value.has_value()) {
            if (std::holds_alternative<std::int64_t>(value) &&
                std::holds_alternative<std::int64_t>(*field.min_value)) {
                if (std::get<std::int64_t>(value) < std::get<std::int64_t>(*field.min_value)) {
                    errors.push_back("Champ inferieur au minimum: " + field.name);
                }
            } else if (std::holds_alternative<double>(value) &&
                       std::holds_alternative<double>(*field.min_value)) {
                if (std::get<double>(value) < std::get<double>(*field.min_value)) {
                    errors.push_back("Champ inferieur au minimum: " + field.name);
                }
            }
        }

        if (field.max_value.has_value()) {
            if (std::holds_alternative<std::int64_t>(value) &&
                std::holds_alternative<std::int64_t>(*field.max_value)) {
                if (std::get<std::int64_t>(value) > std::get<std::int64_t>(*field.max_value)) {
                    errors.push_back("Champ superieur au maximum: " + field.name);
                }
            } else if (std::holds_alternative<double>(value) &&
                       std::holds_alternative<double>(*field.max_value)) {
                if (std::get<double>(value) > std::get<double>(*field.max_value)) {
                    errors.push_back("Champ superieur au maximum: " + field.name);
                }
            }
        }
    }
    return errors;
}

} // namespace sea::infrastructure::runtime