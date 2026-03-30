#pragma once

#include <string>

#include "project.h"
#include "yaml/yaml_schema_parser.h"

namespace sea::application {

// ─────────────────────────────────────────────────────────────
// ImportYamlSchemaUseCase
//
// Charge un fichier YAML et le convertit en Project domaine.
// ─────────────────────────────────────────────────────────────
class ImportYamlSchemaUseCase {
public:
    ImportYamlSchemaUseCase() = default;

    explicit ImportYamlSchemaUseCase(
        sea::infrastructure::yaml::YamlSchemaParser parser)
        : parser_(std::move(parser)) {
    }

    [[nodiscard]] sea::domain::Project execute(const std::string& file_path) const;

private:
    sea::infrastructure::yaml::YamlSchemaParser parser_{};
};

} // namespace sea::application