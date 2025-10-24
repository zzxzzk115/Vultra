#pragma once

#include <cereal/archives/json.hpp>
#include <cereal/types/string.hpp>

#include <cstdint>
#include <string>

namespace vultra
{
    namespace engine
    {
        struct Project
        {
            uint32_t serialVersion {1};

            // Vultra 0.1.0
            uint32_t engineMajorVersion {0};
            uint32_t engineMinorVersion {1};
            uint32_t enginePatchVersion {0};

            std::string name;

            // Cereal serialization
            template<class Archive>
            void serialize(Archive& archive)
            {
                archive(CEREAL_NVP(serialVersion),
                        CEREAL_NVP(engineMajorVersion),
                        CEREAL_NVP(engineMinorVersion),
                        CEREAL_NVP(enginePatchVersion),
                        CEREAL_NVP(name));
            }

            std::string directory;
        };

        bool loadProject(const std::string& filepath, Project& outProject);
        bool saveProject(const std::string& filepath, const Project& project);

        void createDefaultProject(const std::string& projectDir, const std::string& projectName);
    } // namespace engine
} // namespace vultra