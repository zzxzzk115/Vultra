#pragma once

#include <vultra/assets/project_manifest.hpp>
#include <vultra/platform/input/input.hpp>
#include <vultra/scripting/script_properties.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace vultra
{
    class EditorGui;
    class SceneTree;

    // Exports are borrowed only for add(); the host owns its copy after a successful attachment.
    struct ScriptAttachment
    {
        ScriptLanguage                language;
        std::filesystem::path         path;
        std::string                   typeName;
        std::optional<StableId>       node;
        std::span<const ScriptExport> exports {};
    };

    // Each module belongs to one project scene. Call update and gui in their respective frame phases.
    // The optional project is borrowed for the host lifetime and enables model selection by asset ID.
    // Language contexts belong to this host; stop it before replacing its borrowed world/project.
    class ScriptHost
    {
    public:
        explicit ScriptHost(SceneTree& scene, bool hotReload = false, const ProjectManifest* project = nullptr);
        ~ScriptHost();
        ScriptHost(const ScriptHost&)            = delete;
        ScriptHost& operator=(const ScriptHost&) = delete;

        void addNativeModule(const std::filesystem::path& path);
        void add(const ScriptAttachment& attachment);
        // Construct all instances before resolving references and publishing any attachment.
        void add(std::span<const ScriptAttachment> attachments);
        // Resolve scene-owned module IDs through the borrowed project, then publish one validated batch.
        void addScene(const AssetSource& source);
        // Capture non-default serializable fields. Ordinary runtime edits do not rewrite initial exports.
        void captureExports(ObjectId node);
        // Class metadata is borrowed until replacement/stop. Field values stay in the language instance.
        const ScriptClassInfo& scriptClass(ObjectId node) const;
        PropertyValue          readExport(ObjectId node, std::string_view name) const;
        void                   writeExport(ObjectId node, std::string_view name, const PropertyValue& value);
        // Call between callbacks. Stage sources connected by declared class references before publishing bindings.
        // Staging errors keep old instances; post-commit activation errors stop the world and propagate.
        // The count includes standalone instances and provider roots; dependent instances count with their provider.
        size_t           reloadChanged(bool retryFailed = false);
        std::string_view lastReloadError() const;
        void             update(float deltaSeconds);
        // Resolve UI ownership and update the scene InputMap before this overload.
        void update(float deltaSeconds, std::span<InputEvent> events, InputCapture capture = {});
        // Requires api_ui; CPU-only hosts still use the same lifecycle and scene ABI.
        void gui(EditorGui& gui);
        void stop() noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace vultra
