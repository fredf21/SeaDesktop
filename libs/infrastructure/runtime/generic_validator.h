#pragma once

#include "dynamic_record.h"
#include "entity.h"

#include <string>
#include <vector>

namespace sea::infrastructure::runtime {

// ─────────────────────────────────────────────────────────────
// GenericValidator
//
// Vérifie qu’un record dynamique respecte la définition
// d’une entité domaine.
// ─────────────────────────────────────────────────────────────
class GenericValidator {
public:
    [[nodiscard]] std::vector<std::string>
    validate(const sea::domain::Entity& entity,
             const DynamicRecord& record) const;

    [[nodiscard]] std::vector<std::string>
    validate_partial(const sea::domain::Entity& entity,
             const DynamicRecord& record) const;
private:
    [[nodiscard]] bool matches_type(sea::domain::FieldType type,
                                    const DynamicValue& value) const;

    [[nodiscard]] bool is_valid_email(const std::string& value) const;
};

} // namespace sea::infrastructure::runtime