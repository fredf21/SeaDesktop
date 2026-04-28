#pragma once

#include "project.h"

#include <filesystem>

namespace sea::infrastructure::yaml {

class yaml_project_repository {
public:
    sea::domain::Project load(const std::filesystem::path& source) const;
};

} // namespace sea::infrastructure::yaml