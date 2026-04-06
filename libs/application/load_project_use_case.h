#ifndef LOAD_PROJECT_USE_CASE_H
#define LOAD_PROJECT_USE_CASE_H
#include ".././infrastructure/runtime/i_project_repository.h"
namespace sea::application {
    class LoadProjectUseCase {
    public:
        explicit LoadProjectUseCase(IProjectRepository& repo) : _repo(repo){}
        std::vector<sea::domain::Project> execute(const std::string& folder){
            std::vector<sea::domain::Project> projects;

            for (const auto& entry : std::filesystem::directory_iterator(folder)) {
                if (entry.path().extension() == ".yaml") {
                    projects.push_back(_repo.load(entry.path()));
                }
            }

            return projects;
        }
    private:
        IProjectRepository& _repo;
    };
}
#endif // LOAD_PROJECT_USE_CASE_H
