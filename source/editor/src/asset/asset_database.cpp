#include "vultra_editor/asset/asset_database.hpp"

#include <filesystem>

namespace vultra
{
    namespace editor
    {
        AssetDatabase* AssetDatabase::s_Instance = nullptr;

        AssetDatabase::AssetDatabase() : m_AssetImporter(m_AssetRegistry) {}

        void AssetDatabase::initialize(const engine::Project& project)
        {
            m_Project = project;

            std::string workingFolder  = project.directory;
            std::string assetFolder    = project.directory + "/Assets";
            std::string importedFolder = project.directory + "/.imported";

            std::string outputRegistryFile = importedFolder + "/asset_registry.json";

            m_AssetRegistry.setImportedFolder(importedFolder);
            if (std::filesystem::exists(outputRegistryFile))
            {
                m_AssetRegistry.load(outputRegistryFile);
            }

            m_AssetImporter.importAssetFolder(assetFolder);
            if (!m_AssetImporter.importAssetFolder(assetFolder))
            {
                throw std::runtime_error("Failed to import asset folder: " + assetFolder);
            }

            m_AssetRegistry.save(outputRegistryFile);
        }

        AssetDatabase* AssetDatabase::get()
        {
            if (!s_Instance)
            {
                s_Instance = new AssetDatabase();
            }
            return s_Instance;
        }
    } // namespace editor
} // namespace vultra