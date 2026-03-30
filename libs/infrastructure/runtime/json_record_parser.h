#pragma once

#include "dynamic_record.h"
#include "entity.h"

#include <string>

namespace sea::infrastructure::runtime {

// ─────────────────────────────────────────────────────────────
// JsonRecordParser
//
// Convertit un body JSON en DynamicRecord en s'appuyant
// sur la définition d'une entité domaine.
// ─────────────────────────────────────────────────────────────
class JsonRecordParser {
public:
    [[nodiscard]] DynamicRecord parse(const sea::domain::Entity& entity,
                                      const std::string& json_body) const;
};

} // namespace sea::infrastructure::runtime