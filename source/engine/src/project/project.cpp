#include "vultra_engine/project/project.hpp"

#include <nlohmann/json.hpp>

#include <fstream>

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

            nlohmann::json jsonData;
            file >> jsonData;

            // Deserialize JSON data into Project struct
            outProject.serialVersion      = jsonData["serialVersion"];
            outProject.engineMajorVersion = jsonData["engineMajorVersion"];
            outProject.engineMinorVersion = jsonData["engineMinorVersion"];
            outProject.enginePatchVersion = jsonData["enginePatchVersion"];
            outProject.name               = jsonData["name"];

            return true;
        }

        bool saveProject(const std::string& filepath, const Project& project)
        {
            std::ofstream file(filepath);
            if (!file.is_open())
            {
                return false;
            }

            nlohmann::json jsonData;
            jsonData["serialVersion"]      = project.serialVersion;
            jsonData["engineMajorVersion"] = project.engineMajorVersion;
            jsonData["engineMinorVersion"] = project.engineMinorVersion;
            jsonData["enginePatchVersion"] = project.enginePatchVersion;
            jsonData["name"]               = project.name;

            file << jsonData.dump(4); // Pretty print with 4 spaces

            return true;
        }

        void createDefaultProject(const std::string& projectDir, const std::string& projectName)
        {
            Project defaultProject {};
            defaultProject.name = projectName;

            saveProject(projectDir + "/" + projectName + ".vproj", defaultProject);
        }
    } // namespace engine
} // namespace vultra