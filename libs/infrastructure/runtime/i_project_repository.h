#ifndef I_PROJECT_REPOSITORY_H
#define I_PROJECT_REPOSITORY_H
#include "project.h"
#include <filesystem>
class IProjectRepository {
public:
    virtual ~IProjectRepository() = default;
    virtual sea::domain::Project load(const std::filesystem::path& source) const = 0;
};
#endif // I_PROJECT_REPOSITORY_H
