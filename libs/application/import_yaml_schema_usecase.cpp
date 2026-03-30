#include "import_yaml_schema_usecase.h"

namespace sea::application {

sea::domain::Project
ImportYamlSchemaUseCase::execute(const std::string& file_path) const {
    return parser_.parse_project_file(file_path);
}

} // namespace sea::application