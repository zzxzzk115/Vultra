#pragma once

#include <vasset/vasset.hpp>
#include <vultra/core/rhi/texture.hpp>
#include <vultra/function/renderer/imgui_renderer.hpp>
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
            ~AssetDatabase();

            void initialize(const engine::Project& project, rhi::RenderDevice& rd);

            vasset::VAssetRegistry& getRegistry() { return m_AssetRegistry; }
            vasset::VAssetImporter& getImporter() { return m_AssetImporter; }

            std::filesystem::path getAssetRootDir() const
            {
                return std::filesystem::path(m_Project.directory) / "Assets";
            }

            Ref<rhi::Texture>     getTextureByUUID(const vasset::VUUID& uuid);
            imgui::ImGuiTextureID getImGuiTextureByUUID(const vasset::VUUID& uuid);

            static AssetDatabase* get();
            static void           destroy();

            static vasset::VUUID getMetaUUID(const std::filesystem::path& assetPath);

        private:
            rhi::RenderDevice* m_RenderDevice {nullptr};

            vasset::VAssetRegistry m_AssetRegistry;
            vasset::VAssetImporter m_AssetImporter;
            engine::Project        m_Project;

            // UUID string to Texture
            std::unordered_map<std::string, Ref<rhi::Texture>> m_Textures;
            // UUID string to ImGuiTextureID
            std::unordered_map<std::string, imgui::ImGuiTextureID> m_ImGuiTextures;

            static AssetDatabase* s_Instance;
        };
    } // namespace editor
} // namespace vultra