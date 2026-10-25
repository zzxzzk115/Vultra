#pragma once

#include <vultra/assets/asset_entry.hpp>
#include <vultra/assets/fbx_import.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vultra
{
    class AssetSource;

    enum class ScriptLanguage
    {
        eNative,
        eLua,
        eCSharp
    };

    // Module identity belongs to the project; class selection and initial values belong to scene nodes.
    struct ScriptModule
    {
        AssetId        asset;
        ScriptLanguage language;
    };

    // The project's stable path-to-ID map. Renaming an entry preserves references in scenes.
    class ProjectManifest
    {
    public:
        std::filesystem::path  mainScene;
        std::optional<AssetId> environment;
        std::optional<AssetId> uiDocument;
        std::optional<AssetId> uiFont;
        std::vector<AssetId>   extensions; // Native type modules, installed in dependency order before scene loading.
        std::vector<ScriptModule> scripts;
        std::string               metadata = "{}"; // JSON object interpreted by optional app/extension code.

        AssetId addAsset(const std::filesystem::path& path, std::optional<FbxImportOptions> fbx = std::nullopt);
        void    renameAsset(AssetId id, const std::filesystem::path& path);
        const AssetEntry&              asset(AssetId id) const;
        const std::vector<AssetEntry>& assets() const;
        const ScriptModule&            scriptModule(AssetId id) const;

        // Retain the source until type owners and script hosts retire; loaders borrow these real files.
        std::filesystem::path materializeModule(const AssetSource& source, const std::filesystem::path& module) const;

        void                   save(const std::filesystem::path& file) const;
        static ProjectManifest load(const std::filesystem::path& file, const AssetSource* source = nullptr);

    private:
        void                    validate() const;
        std::vector<AssetEntry> m_Assets;
    };
} // namespace vultra
