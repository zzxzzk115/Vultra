#include "vultra_editor/asset/asset_database.hpp"

#include <vultra/function/renderer/texture_manager.hpp>
#include <vultra/function/resource/resource.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace vultra
{
    namespace editor
    {
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

            // Load all textures to memory
            for (const auto& [uuidStr, entry] : m_AssetRegistry.getRegistry())
            {
                if (entry.type == vasset::VAssetType::eTexture)
                {
                    auto texturePath = std::filesystem::path(importedFolder) / entry.path;
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

        Ref<rhi::Texture> AssetDatabase::getTextureByUUID(const vasset::VUUID& uuid)
        {
            auto uuidStr = uuid.toString();
            auto it = m_Textures.find(uuidStr);
            if (it != m_Textures.end())
            {
                return it->second;
            }
            return nullptr;
        }

        imgui::ImGuiTextureID AssetDatabase::getImGuiTextureByUUID(const vasset::VUUID& uuid)
        {
            auto uuidStr = uuid.toString();
            auto it = m_ImGuiTextures.find(uuidStr);
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
            std::filesystem::path metaPath = assetPath;
            metaPath.replace_extension(".vmeta");

            if (!std::filesystem::exists(metaPath))
            {
                return vasset::VUUID {};
            }

            // Load meta file
            std::ifstream inFile(metaPath);
            if (!inFile.is_open())
            {
                return vasset::VUUID {};
            }

            nlohmann::json j;
            inFile >> j;

            if (j.contains("uuid"))
            {
                auto uuidStr = j["uuid"].get<std::string>();
                return vasset::VUUID::fromString(uuidStr);
            }

            return vasset::VUUID {};
        }
    } // namespace editor
} // namespace vultra