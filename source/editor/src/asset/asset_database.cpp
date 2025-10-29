#include "vultra_editor/asset/asset_database.hpp"

#include <vultra/function/renderer/texture_manager.hpp>
#include <vultra/function/resource/resource.hpp>

#include <filesystem>
#include <fstream>

namespace vultra
{
    namespace editor
    {
        constexpr const char* ASSET_REGISTRY_FILE = "asset_registry.json";
        constexpr const char* ASSET_IMPORT_FOLDER = "Assets";
        constexpr const char* ASSET_EXPORT_FOLDER = ".imported";
        constexpr const char* META_FILE_EXTENSION = ".vmeta";

        AssetDatabase* AssetDatabase::s_Instance = nullptr;

        AssetDatabase::AssetDatabase() : m_AssetImporter(m_AssetRegistry) {}

        AssetDatabase::~AssetDatabase()
        {
            // Cleanup ImGui textures
            for (auto& [uuidStr, imguiTexID] : m_ImGuiTextures)
            {
                imgui::removeTexture(*m_RenderDevice, imguiTexID);
            }
        }

        void AssetDatabase::initialize(const engine::Project& project, rhi::RenderDevice& rd)
        {
            m_Project      = project;
            m_RenderDevice = &rd;

            // Setup paths
            m_Paths.workingDir   = project.directory;
            m_Paths.assetDir     = m_Paths.workingDir / ASSET_IMPORT_FOLDER;
            m_Paths.importedDir  = m_Paths.workingDir / ASSET_EXPORT_FOLDER;
            m_Paths.registryFile = m_Paths.importedDir / ASSET_REGISTRY_FILE;

            // Get string paths
            std::string workingFolder      = m_Paths.workingDir.string();
            std::string assetFolder        = m_Paths.assetDir.string();
            std::string importedFolder     = m_Paths.importedDir.string();
            std::string outputRegistryFile = m_Paths.registryFile.string();

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

            // Load all textures to memory
            for (const auto& [uuidStr, entry] : m_AssetRegistry.getRegistry())
            {
                if (entry.type == vasset::VAssetType::eTexture)
                {
                    auto texturePath = m_Paths.importedDir / entry.path;
                    auto texture     = resource::loadResource<gfx::TextureManager>(texturePath.generic_string());
                    if (!texture)
                    {
                        VULTRA_CORE_ERROR("Failed to load texture asset: {}", entry.path);
                        continue;
                    }

                    // Store texture
                    m_Textures[uuidStr] = texture;

                    // Add imgui texture
                    m_ImGuiTextures[uuidStr] = imgui::addTexture(*texture);
                }
            }
        }

        bool AssetDatabase::renameAsset(const vasset::VUUID& uuid,
                                        const std::string&   oldName,
                                        const std::string&   newName,
                                        const std::string&   parentDir)
        {
            // Find asset entry
            auto entry = m_AssetRegistry.lookup(uuid);
            if (entry.path.empty())
            {
                return false;
            }

            try
            {
                auto parentPath = std::filesystem::path(parentDir);
                auto oldPath    = parentPath / oldName;
                auto newPath    = parentPath / newName;

                // Get old and new meta file paths
                std::filesystem::path assetMetaPath    = oldPath.replace_extension(META_FILE_EXTENSION);
                std::filesystem::path newAssetMetaPath = newPath.replace_extension(META_FILE_EXTENSION);

                // Get original asset file path from meta
                auto originalFileExtension = getMetaExtension(assetMetaPath);
                if (originalFileExtension.empty())
                {
                    VULTRA_CORE_ERROR("Failed to get original asset file extension from meta");
                    return false;
                }
                auto oldPathCopy      = oldPath;
                auto originalFilePath = oldPathCopy.replace_extension(originalFileExtension);
                // Get new original asset file path
                auto newAssetPath = originalFilePath.parent_path() / (newName + originalFilePath.extension().string());

                // Rename files
                std::filesystem::rename(assetMetaPath, newAssetMetaPath);
                std::filesystem::rename(originalFilePath, newAssetPath);

                // Get imported path
                std::filesystem::path importedPath = m_Paths.importedDir / entry.path;

                // Get old and new imported paths
                const auto& oldImportedPath = importedPath;
                auto newImportedPath = importedPath.parent_path() / (newName + importedPath.extension().string());

                // Update asset registry
                if (!m_AssetRegistry.updateRegistry(
                        uuid, std::filesystem::relative(newImportedPath, m_Paths.importedDir).string()))
                {
                    VULTRA_CORE_ERROR("Failed to update asset registry");
                    return false;
                }

                // Rename imported file
                std::filesystem::rename(oldImportedPath, newImportedPath);

                // Save registry
                m_AssetRegistry.save(m_Paths.registryFile.string());
            }
            catch (const std::exception& e)
            {
                VULTRA_CORE_ERROR("Failed to rename asset: {}", e.what());
                return false;
            }

            return true;
        }

        bool AssetDatabase::reimportAsset(const std::filesystem::path& assetPath)
        {
            auto metaUUID          = getMetaUUID(assetPath);
            auto metaExt           = getMetaExtension(assetPath);
            auto assetPathCopy     = assetPath;
            auto originalAssetPath = assetPathCopy.replace_extension(metaExt);

            if (!m_AssetImporter.importOrReimportAsset(originalAssetPath.string(), true))
            {
                return false;
            }

            // Update textures and ImGui textures if needed
            auto entry = m_AssetRegistry.lookup(metaUUID);
            if (entry.type == vasset::VAssetType::eTexture)
            {
                auto texturePath = m_Paths.importedDir / entry.path;

                // We don't use resource::loadResource here to ensure we don't get a cached version
                gfx::TextureLoader tmpTextureLoader {};

                auto texture = tmpTextureLoader(texturePath.generic_string(), *m_RenderDevice);
                if (!texture)
                {
                    VULTRA_CORE_ERROR("Failed to load texture asset: {}", entry.path);
                    return false;
                }

                auto uuidStr = metaUUID.toString();

                // Store texture
                m_Textures[uuidStr] = texture;

                // Add imgui texture
                m_ImGuiTextures[uuidStr] = imgui::addTexture(*texture);
            }

            return true;
        }

        Ref<rhi::Texture> AssetDatabase::getTextureByUUID(const vasset::VUUID& uuid)
        {
            auto uuidStr = uuid.toString();
            auto it      = m_Textures.find(uuidStr);
            if (it != m_Textures.end())
            {
                return it->second;
            }
            return nullptr;
        }

        imgui::ImGuiTextureID AssetDatabase::getImGuiTextureByUUID(const vasset::VUUID& uuid)
        {
            auto uuidStr = uuid.toString();
            auto it      = m_ImGuiTextures.find(uuidStr);
            if (it != m_ImGuiTextures.end())
            {
                return it->second;
            }

            // Load ImGui texture
            Ref<rhi::Texture> texture = getTextureByUUID(uuid);
            if (texture && m_RenderDevice)
            {
                imgui::ImGuiTextureID imguiTexID = imgui::addTexture(*texture);
                m_ImGuiTextures[uuid.toString()] = imguiTexID;
                return imguiTexID;
            }

            return nullptr;
        }

        AssetDatabase* AssetDatabase::get()
        {
            if (!s_Instance)
            {
                s_Instance = new AssetDatabase();
            }
            return s_Instance;
        }

        void AssetDatabase::destroy()
        {
            delete s_Instance;
            s_Instance = nullptr;
        }

        vasset::VUUID AssetDatabase::getMetaUUID(const std::filesystem::path& assetPath)
        {
            nlohmann::json j = getMetaJson(assetPath);

            if (j.contains("uuid"))
            {
                auto uuidStr = j["uuid"].get<std::string>();
                return vasset::VUUID::fromString(uuidStr);
            }

            return vasset::VUUID {};
        }

        std::string AssetDatabase::getMetaExtension(const std::filesystem::path& assetPath)
        {
            nlohmann::json j = getMetaJson(assetPath);

            if (j.contains("extension"))
            {
                return j["extension"].get<std::string>();
            }

            return {};
        }

        nlohmann::json AssetDatabase::getMetaJson(const std::filesystem::path& assetPath)
        {
            std::filesystem::path metaPath = assetPath;
            metaPath.replace_extension(META_FILE_EXTENSION);

            std::ifstream inFile(metaPath);
            if (!inFile.is_open())
            {
                return nlohmann::json {};
            }

            nlohmann::json j;
            inFile >> j;
            inFile.close();
            return j;
        }
    } // namespace editor
} // namespace vultra