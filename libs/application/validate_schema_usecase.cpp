#include "validate_schema_usecase.h"

namespace sea::application {

ValidationResult
ValidateSchemaUseCase::execute(const sea::domain::Service& service) const {
    ValidationResult result{};

    result.errors = validator_.validate(service.schema);
    result.valid = result.errors.empty();

    return result;
}

} // namespace sea::application