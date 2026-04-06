#ifndef YAML_PROJECT_REPOSITORY_H
#define YAML_PROJECT_REPOSITORY_H

#include "runtime/i_project_repository.h"
#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;
namespace sea::infrastructure::yaml {
    class yaml_project_repository : public IProjectRepository
    {
    public:
        yaml_project_repository() = default;;
        // IProjectRepository interface
        sea::domain::Project load(const std::filesystem::path &source) const override;
    private:
        sea::domain::Relation parse_relation_node(const YAML::Node& node) const;
        sea::domain::Service parse_service_node(const YAML::Node& node) const;
        sea::domain::Entity parse_entity_node(const YAML::Node& node) const;
        sea::domain::Field parse_field_node(const YAML::Node& node) const;
        sea::domain::RelationKind parse_relation_kind(const std::string& value) const;
        sea::domain::OnDelete parse_on_delete(const std::string& value) const;
        std::string require_string(
            const YAML::Node& node,
            const char* key,
            const char* context
            ) const;

        template<typename T>
        T get_or_default(
            const YAML::Node& node,
            const char* key,
            const T& default_value
            ) const {
            if (!node || !node[key]) {
                return default_value;
            }

            try {
                return node[key].as<T>();
            } catch (const YAML::Exception& e) {
                throw std::runtime_error(
                    std::string("Valeur invalide pour '") + key + "': " + e.what()
                    );
            }
        }
    };

}
#endif // YAML_PROJECT_REPOSITORY_H
