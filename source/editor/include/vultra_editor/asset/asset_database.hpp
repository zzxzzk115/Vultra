#pragma once

#include <vasset/vasset.hpp>
#include <vultra_engine/project/project.hpp>

#include <filesystem>

namespace vultra
{
    namespace editor
    {
        class AssetDatabase
        {
        public:
            AssetDatabase();
            ~AssetDatabase() = default;

            void initialize(const engine::Project& project);

            vasset::VAssetRegistry& getRegistry() { return m_AssetRegistry; }
            vasset::VAssetImporter& getImporter() { return m_AssetImporter; }

            std::filesystem::path getAssetRootDir() const
            {
                return std::filesystem::path(m_Project.directory) / "Assets";
            }

            static AssetDatabase* get();

        private:
            vasset::VAssetRegistry m_AssetRegistry;
            vasset::VAssetImporter m_AssetImporter;
            engine::Project        m_Project;

            static AssetDatabase* s_Instance;
        };
    } // namespace editor
} // namespace vultra