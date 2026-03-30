#pragma once

#include <string>
#include <vector>

#include "schema_validator.h"
#include "service.h"

namespace sea::application {

// ─────────────────────────────────────────────────────────────
// Résultat de validation d’un service
// ─────────────────────────────────────────────────────────────
struct ValidationResult {
    bool valid{true};
    std::vector<std::string> errors;
};

// ─────────────────────────────────────────────────────────────
// ValidateSchemaUseCase
//
// Applique SchemaValidator sur le schéma d’un service.
// ─────────────────────────────────────────────────────────────
class ValidateSchemaUseCase {
public:
    ValidateSchemaUseCase() = default;

    explicit ValidateSchemaUseCase(sea::domain::SchemaValidator validator)
        : validator_(std::move(validator)) {
    }

    [[nodiscard]] ValidationResult execute(const sea::domain::Service& service) const;

private:
    sea::domain::SchemaValidator validator_{};
};

} // namespace sea::application