#include "vultra_engine/project/project.hpp"

#include <fstream>

constexpr auto PROJECT_NVP = "project";

namespace vultra
{
    namespace engine
    {
        bool loadProject(const std::string& filepath, Project& outProject)
        {
            std::ifstream file(filepath);
            if (!file.is_open())
            {
                return false;
            }

            // Cereal JSON deserialization
            cereal::JSONInputArchive archive(file);
            archive(cereal::make_nvp(PROJECT_NVP, outProject));

            return true;
        }

        bool saveProject(const std::string& filepath, const Project& project)
        {
            std::ofstream file(filepath);
            if (!file.is_open())
            {
                return false;
            }

            // Cereal JSON serialization
            cereal::JSONOutputArchive archive(file);
            archive(cereal::make_nvp(PROJECT_NVP, project));

            return true;
        }

        void createDefaultProject(const std::string& projectDir, const std::string& projectName)
        {
            Project defaultProject {};
            defaultProject.name      = projectName;
            defaultProject.directory = projectDir;

            saveProject(projectDir + "/" + projectName + ".vproj", defaultProject);
        }
    } // namespace engine
} // namespace vultra