#include "yaml_project_repository.h"
#include "yaml_schema_parser.h"

#include <stdexcept>

namespace sea::infrastructure::yaml {

sea::domain::Project yaml_project_repository::load(
    const std::filesystem::path& source
    ) const
{
    if (!std::filesystem::exists(source)) {
        throw std::runtime_error(
            "Fichier YAML introuvable : " + source.string()
            );
    }

    YamlSchemaParser parser;
    return parser.parse_project_file(source.string());
}

} // namespace sea::infrastructure::yaml