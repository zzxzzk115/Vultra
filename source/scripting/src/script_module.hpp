#pragma once

#include <vultra/api/native_plugin.h>
#include <vultra/api/scene_bridge.hpp>
#include <vultra/core/base/stable_id.hpp>
#include <vultra/scene/node.hpp>
#include <vultra/scene/scene_components.hpp>
#include <vultra/scripting/script_properties.hpp>

#include <filesystem>
#include <memory>
#include <string_view>

namespace vultra
{
    class EditorGui;
    class SceneTree;
    class ProjectManifest;
    class LuaContext;

    class ScriptInstance : public NodeBehavior
    {
    public:
        virtual ~ScriptInstance()                                           = default;
        virtual void update(float deltaSeconds)                             = 0;
        virtual void gui(EditorGui& gui)                                    = 0;
        virtual void stop() noexcept                                        = 0;
        virtual void notify(VultraSceneEvent event, float deltaSeconds = 0) = 0;

        const ScriptClassInfo&     scriptClass() const;
        const NodePropertyBinding& propertyBinding() const;
        PropertyValue              readExport(std::string_view name) const;
        void                       writeExport(std::string_view name, const PropertyValue& value);
        // Qualify declaration-local class names with the original source, never a staged reload filename.
        void bindClassOrigin(const std::filesystem::path& source);
        void prepareProperties();

        void enterTree() final
        {
            notify(VULTRA_SCENE_ENTER_TREE);
        }

        void ready() final
        {
            notify(VULTRA_SCENE_READY);
        }

        void physicsProcess(double deltaSeconds) final
        {
            notify(VULTRA_SCENE_PHYSICS_PROCESS, static_cast<float>(deltaSeconds));
        }

        void process(double deltaSeconds) final
        {
            update(static_cast<float>(deltaSeconds));
        }

        void input(InputEvent&) final
        {
            if (m_Class.input)
            {
                notify(VULTRA_SCENE_INPUT);
            }
        }

        void unhandledInput(InputEvent&) final
        {
            if (m_Class.unhandledInput)
            {
                notify(VULTRA_SCENE_UNHANDLED_INPUT);
            }
        }

        void exitTree() final
        {
            notify(VULTRA_SCENE_EXIT_TREE);
        }

    protected:
        void bindProperties(const VultraScriptPropertyApi* api,
                            void*                          state,
                            SceneTree&                     scene,
                            const ProjectManifest*         project);

    private:
        const VultraScriptPropertyApi* m_PropertyApi   = nullptr;
        void*                          m_PropertyState = nullptr;
        ScriptClassInfo                m_Class;
        NodePropertyBinding            m_PropertyBinding;
        mutable SceneAccess            m_PropertyAccess;
    };

    std::unique_ptr<ScriptInstance>
    loadLuaScript(const std::filesystem::path& path, LuaContext& context, ObjectId node);
    std::unique_ptr<ScriptInstance> loadDotNetScript(const std::filesystem::path& path,
                                                     SceneTree&                   scene,
                                                     ObjectId                     node,
                                                     std::string_view             typeName,
                                                     const ScriptInstance*        previous  = nullptr,
                                                     const ProjectManifest*       project   = nullptr,
                                                     const ScriptInstance*        peer      = nullptr,
                                                     const ScriptInstance*        batchPeer = nullptr);
} // namespace vultra
