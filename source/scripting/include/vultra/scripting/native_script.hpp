#pragma once

#include <vultra/api/native_plugin.h>
#include <vultra/api/script_property_callbacks.generated.hpp>
#include <vultra/scripting/native_script_properties.hpp>
#include <vultra/scripting/script_input.hpp>

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <vector>

namespace vultra::scripting
{
    struct ActionState
    {
        bool held;
        bool pressed;
        bool released;
    };

    struct NativeScriptInit
    {
        uint64_t    nodeId;
        const char* typeName;
        uint64_t    typeNameSize;
    };

    using CameraSettings      = VultraCameraSettings;
    using LightSettings       = VultraLightSettings;
    using MaterialParameters  = VultraMaterialParameters;
    using EnvironmentSettings = VultraEnvironmentSettings;

    enum class ProcessMode : uint64_t
    {
        eInherit,
        ePausable,
        eWhenPaused,
        eAlways,
        eDisabled
    };

    enum class LightKind : uint64_t
    {
        eDirectional,
        ePoint,
        eSpot
    };

    class NativeScript;
    class SceneResource;
    class SceneMaterial;

    struct NativeScriptClass
    {
        std::string name;
        std::unique_ptr<NativeScript> (*create)();
        std::vector<NativeScriptProperty> properties;
        std::string                       baseType;
        std::type_index                   cppType {typeid(void)};
        std::vector<std::string>          classes;
        bool                              input          = false;
        bool                              unhandledInput = false;
    };

    class SceneNode
    {
    public:
        Float3                       position() const;
        void                         setPosition(Float3 position) const;
        SceneNode                    createChild(std::string_view name) const;
        SceneNode                    createChild3D(std::string_view name) const;
        SceneNode                    createMeshChild(std::string_view name, std::string_view assetId) const;
        SceneNode                    createCameraChild(std::string_view name) const;
        SceneNode                    createLightChild(std::string_view name, LightKind kind) const;
        SceneNode                    createEnvironmentChild(std::string_view name) const;
        EnvironmentSettings          environmentSettings() const;
        void                         setEnvironmentSettings(EnvironmentSettings settings) const;
        void                         setEnvironmentAsset(std::string_view assetId) const;
        void                         makeEnvironmentCurrent() const;
        CameraSettings               cameraSettings() const;
        void                         setCameraSettings(CameraSettings settings) const;
        void                         makeCurrent() const;
        LightKind                    lightKind() const;
        LightSettings                lightSettings() const;
        void                         setLightSettings(LightSettings settings) const;
        SceneNode                    duplicateMesh(SceneNode parent) const;
        void                         reparent(SceneNode newParent, bool keepGlobalTransform = true) const;
        void                         copyMeshModel(SceneNode source) const;
        void                         setMeshModel(std::string_view assetId) const;
        std::optional<SceneMaterial> material(uint64_t slot) const;
        void                         setMaterial(uint64_t slot, const SceneMaterial& material) const;
        void                         clearMaterial(uint64_t slot) const;
        void                         remove() const;
        SceneNode                    child(uint64_t index) const;
        uint64_t                     childCount() const;
        std::string_view             name() const;
        std::string                  propertyJson(std::string_view name) const;
        std::string                  propertyListJson() const;
        void                         setPropertyJson(std::string_view name, std::string_view json) const;
        std::vector<std::string>     components() const;
        std::string                  componentPropertyJson(std::string_view component, std::string_view name) const;
        void setComponentPropertyJson(std::string_view component, std::string_view name, std::string_view json) const;
        std::string_view         typeName() const;
        std::optional<SceneNode> parent() const;
        std::optional<SceneNode> owner() const;
        void                     setOwner(std::optional<SceneNode> owner) const;
        ProcessMode              processMode() const;
        void                     setProcessMode(ProcessMode mode) const;
        int32_t                  processPriority() const;
        void                     setProcessPriority(int32_t priority) const;
        bool                     inputEnabled() const;
        void                     setInputEnabled(bool enabled) const;
        bool                     unhandledInputEnabled() const;
        void                     setUnhandledInputEnabled(bool enabled) const;
        bool                     isInsideTree() const;
        bool                     isQueuedForDeletion() const;
        void                     queueFree() const;

        NodeReference reference() const
        {
            return {m_Id};
        }

    private:
        friend class NativeScript;

        SceneNode(const NativeScript& script, uint64_t id) :
            m_Script(script),
            m_Id(id)
        {
        }

        const NativeScript& m_Script;
        uint64_t            m_Id;
    };

    class SceneResource
    {
    public:
        std::string_view typeName() const;
        std::string      propertyJson(std::string_view name) const;
        void             setPropertyJson(std::string_view name, std::string_view json) const;
        void             remove() const;

        ResourceReference reference() const
        {
            return {m_Id};
        }

    protected:
        friend class NativeScript;

        SceneResource(const NativeScript& script, uint64_t id) :
            m_Script(script),
            m_Id(id)
        {
        }

        const NativeScript& m_Script;
        uint64_t            m_Id;
    };

    class SceneMaterial : public SceneResource
    {
    public:
        MaterialParameters parameters() const;
        void               setParameters(MaterialParameters parameters) const;
        std::string_view   name() const;

    private:
        friend class NativeScript;
        friend class SceneNode;

        SceneMaterial(const NativeScript& script, uint64_t id) :
            SceneResource(script, id)
        {
        }
    };

    class EditorGui
    {
    public:
        void text(std::string_view message) const
        {
            check(m_Api.text(m_Frame, message.data(), message.size()));
        }

        bool button(std::string_view label) const
        {
            uint8_t clicked = 0;
            check(m_Api.button(m_Frame, label.data(), label.size(), &clicked));
            return clicked != 0;
        }

    private:
        friend class NativeScript;

        EditorGui(const VultraUiApi& api, VultraUiFrame frame) :
            m_Api(api),
            m_Frame(frame)
        {
        }

        static void check(VultraStatus status)
        {
            if (status != VULTRA_STATUS_OK)
            {
                throw std::runtime_error("Vultra UI API call failed: " + std::to_string(int(status)));
            }
        }

        const VultraUiApi& m_Api;
        VultraUiFrame      m_Frame;
    };

    class NativeScript
    {
    public:
        virtual ~NativeScript() = default;

        virtual void onEnterTree()
        {
        }

        virtual void onReady()
        {
        }

        virtual void onPhysicsProcess(float deltaSeconds)
        {
        }

        virtual void onProcess(float deltaSeconds)
        {
        }

        virtual void onInput(const InputEvent& event)
        {
        }

        virtual void onUnhandledInput(const InputEvent& event)
        {
        }

        ActionState inputAction(const InputEvent& event, std::string_view name) const
        {
            requireInputFrame(event);
            uint64_t value = 0;
            check(m_Host.scene->input_action_state(m_Frame, event.m_Data.serial, name.data(), name.size(), &value));
            return {(value & 1) != 0, (value & 2) != 0, (value & 4) != 0};
        }

        void acceptInput(const InputEvent& event) const
        {
            requireInputFrame(event);
            check(m_Host.scene->set_input_handled(m_Frame, event.m_Data.serial));
        }

        virtual void onGui(const EditorGui& gui)
        {
        }

        virtual void onExitTree()
        {
        }

        ActionState action(std::string_view name) const
        {
            uint64_t value = 0;
            check(m_Host.scene->action_state(m_Frame, name.data(), name.size(), &value));
            return {(value & 1) != 0, (value & 2) != 0, (value & 4) != 0};
        }

        std::string describePropertiesJson() const
        {
            return m_PropertyDescription;
        }

        std::string readExportJson(VultraSceneFrame frame, std::string_view name)
        {
            PropertyFrameScope scope(*this, frame);
            return exportField(name).read(*this, *m_Host.scene, m_Frame).dump();
        }

        void writeExportJson(VultraSceneFrame frame, std::string_view name, std::string_view json)
        {
            PropertyFrameScope scope(*this, frame);
            exportField(name).write(*this, *m_Host.scene, m_Frame, nlohmann::json::parse(json));
        }

    protected:
        SceneNode resolve(NodeReference reference) const
        {
            return {*this, reference.id};
        }

        SceneResource resolve(ResourceReference reference) const
        {
            return {*this, reference.id};
        }

        SceneMaterial                createMaterial(std::string_view name) const;
        SceneResource                createResource(std::string_view type, std::string_view state = "{}") const;
        std::optional<SceneResource> findResource(std::string_view resourceId) const;
        std::vector<std::string>     typeNames() const;
        std::string                  describeTypeJson(std::string_view type) const;

        bool paused() const
        {
            uint64_t value = 0;
            check(m_Host.scene->paused(m_Frame, &value));
            return value != 0;
        }

        void setPaused(bool paused) const
        {
            check(m_Host.scene->set_paused(m_Frame, paused));
        }

        uint64_t physicsFrame() const
        {
            uint64_t value = 0;
            check(m_Host.scene->physics_frame(m_Frame, &value));
            return value;
        }

        uint32_t randomSeed() const
        {
            uint64_t value = 0;
            check(m_Host.scene->random_seed(m_Frame, &value));
            return uint32_t(value);
        }

        uint32_t randomUInt(std::string_view stream) const
        {
            uint64_t value = 0;
            check(m_Host.scene->random_uint(m_Frame, stream.data(), stream.size(), &value));
            return uint32_t(value);
        }

        uint32_t randomUInt(std::string_view stream, uint32_t bound) const
        {
            uint64_t value = 0;
            check(m_Host.scene->random_bounded_uint(m_Frame, stream.data(), stream.size(), bound, &value));
            return uint32_t(value);
        }

        double randomUnit(std::string_view stream) const
        {
            double value = 0;
            check(m_Host.scene->random_unit(m_Frame, stream.data(), stream.size(), &value));
            return value;
        }

        double timeScale() const
        {
            double value = 0;
            check(m_Host.scene->time_scale(m_Frame, &value));
            return value;
        }

        void setTimeScale(double scale) const
        {
            check(m_Host.scene->set_time_scale(m_Frame, scale));
        }

        void clearCurrentCamera() const
        {
            check(m_Host.scene->set_current_camera(m_Frame, 0));
        }

        void clearCurrentEnvironment() const
        {
            check(m_Host.scene->set_current_environment(m_Frame, 0));
        }

        SceneNode actor() const
        {
            return {*this, m_NodeId};
        }

        SceneNode sceneRoot() const
        {
            uint64_t id = 0;
            check(m_Host.scene->root_id(m_Frame, &id));
            return {*this, id};
        }

    private:
        friend class SceneNode;
        friend class SceneMaterial;
        friend class SceneResource;
        friend VultraStatus
        initializeNativeModule(const VultraHostApi*, VultraPluginApi*, std::span<const NativeScriptClass>);

        std::string readPropertyJson(uint64_t id, std::string_view name) const;
        void        writePropertyJson(uint64_t id, std::string_view name, std::string_view json) const;

        static void check(VultraStatus status)
        {
            if (status != VULTRA_STATUS_OK)
            {
                throw std::runtime_error("Vultra scene API call failed: " + std::to_string(int(status)));
            }
        }

        struct PropertyFrameScope
        {
            NativeScript&    script;
            VultraSceneFrame previous;

            PropertyFrameScope(NativeScript& script, VultraSceneFrame frame) :
                script(script),
                previous(script.m_Frame)
            {
                script.m_Frame = frame;
            }

            ~PropertyFrameScope()
            {
                script.m_Frame = previous;
            }

            PropertyFrameScope(const PropertyFrameScope&)            = delete;
            PropertyFrameScope& operator=(const PropertyFrameScope&) = delete;
        };

        const NativeScriptProperty& exportField(std::string_view name) const
        {
            for (const auto& property : m_Exports)
            {
                if (property.name == name)
                {
                    return property;
                }
            }
            throw std::invalid_argument("Unknown native script export: " + std::string(name));
        }

        void attach(const VultraHostApi& host, uint64_t nodeId, const NativeScriptClass& entry)
        {
            m_Host         = host;
            m_Host.scripts = nullptr;
            m_NodeId       = nodeId;
            m_Exports      = entry.properties;
            nlohmann::json description {{"version", 1},
                                        {"category", "script"},
                                        {"name", entry.name},
                                        {"base", entry.baseType},
                                        {"properties", nlohmann::json::array()}};
            description["classes"] = nlohmann::json::array();
            for (const auto& name : entry.classes)
            {
                description["classes"].push_back({{"module", ""}, {"name", name}});
            }
            description["dependencies"] = nlohmann::json::array();
            description["callbacks"]    = nlohmann::json::array();
            if (entry.input)
            {
                description["callbacks"].push_back("input");
            }
            if (entry.unhandledInput)
            {
                description["callbacks"].push_back("unhandled_input");
            }
            for (const auto& property : m_Exports)
            {
                auto schema       = property.schema;
                schema["default"] = property.read(*this, *m_Host.scene, m_Frame);
                description["properties"].push_back(std::move(schema));
            }
            m_PropertyDescription = description.dump();
        }

        static VultraStatus update(void* userData, VultraSceneFrame frame, float deltaSeconds)
        {
            auto& script   = *static_cast<NativeScript*>(userData);
            script.m_Frame = frame;
            try
            {
                script.onProcess(deltaSeconds);
                script.m_Frame = {};
                return VULTRA_STATUS_OK;
            }
            catch (const std::exception& error)
            {
                std::fprintf(stderr, "Native script update: %s\n", error.what());
                script.m_Frame = {};
                return VULTRA_STATUS_ERROR;
            }
        }

        static VultraStatus gui(void* userData, VultraUiFrame frame)
        {
            auto& script = *static_cast<NativeScript*>(userData);
            if (!script.m_Host.ui)
            {
                return VULTRA_STATUS_INVALID_ARGUMENT;
            }
            try
            {
                script.onGui(EditorGui(*script.m_Host.ui, frame));
                return VULTRA_STATUS_OK;
            }
            catch (const std::exception& error)
            {
                std::fprintf(stderr, "Native script UI: %s\n", error.what());
                return VULTRA_STATUS_ERROR;
            }
        }

        static VultraStatus stop(void* userData)
        {
            std::unique_ptr<NativeScript> script(static_cast<NativeScript*>(userData));
            return VULTRA_STATUS_OK;
        }

        static VultraStatus notify(void* userData, VultraSceneFrame frame, uint32_t event, float deltaSeconds)
        {
            auto&              script = *static_cast<NativeScript*>(userData);
            PropertyFrameScope scope(script, frame);
            try
            {
                switch (event)
                {
                    case VULTRA_SCENE_ENTER_TREE:
                        script.onEnterTree();
                        break;
                    case VULTRA_SCENE_READY:
                        script.onReady();
                        break;
                    case VULTRA_SCENE_PHYSICS_PROCESS:
                        script.onPhysicsProcess(deltaSeconds);
                        break;
                    case VULTRA_SCENE_EXIT_TREE:
                        script.onExitTree();
                        break;
                    case VULTRA_SCENE_RELOADED:
                        break;
                    case VULTRA_SCENE_INPUT:
                        script.onInput(script.currentInput());
                        break;
                    case VULTRA_SCENE_UNHANDLED_INPUT:
                        script.onUnhandledInput(script.currentInput());
                        break;
                    default:
                        return VULTRA_STATUS_INVALID_ARGUMENT;
                }
                return VULTRA_STATUS_OK;
            }
            catch (const std::exception& error)
            {
                std::fprintf(stderr, "Native script scene callback: %s\n", error.what());
                return VULTRA_STATUS_ERROR;
            }
        }

        void requireInputFrame(const InputEvent& event) const
        {
            if (!m_Frame.context || m_NodeId != event.m_NodeId || m_Frame.context != event.m_Frame.context ||
                m_Frame.serial != event.m_Frame.serial)
            {
                throw std::logic_error("Input event requires its original callback");
            }
        }

        InputEvent currentInput() const
        {
            InputEvent event;
            event.m_Frame  = m_Frame;
            event.m_NodeId = m_NodeId;
            check(m_Host.scene->input_event(m_Frame, &event.m_Data));
            const char* text = nullptr;
            uint64_t    size = 0;
            check(m_Host.scene->input_text(m_Frame, event.m_Data.serial, &text, &size));
            event.m_Text.assign(text ? text : "", size);
            return event;
        }

        VultraHostApi                         m_Host {};
        VultraSceneFrame                      m_Frame {};
        uint64_t                              m_NodeId = 0;
        std::span<const NativeScriptProperty> m_Exports;
        std::string                           m_PropertyDescription;
    };

    inline SceneMaterial NativeScript::createMaterial(std::string_view name) const
    {
        uint64_t id = 0;
        check(m_Host.scene->create_material(m_Frame, name.data(), name.size(), &id));
        return {*this, id};
    }

    inline ProcessMode SceneNode::processMode() const
    {
        uint64_t value = 0;
        NativeScript::check(m_Script.m_Host.scene->node_process_mode(m_Script.m_Frame, m_Id, &value));
        return static_cast<ProcessMode>(value);
    }

    inline void SceneNode::setProcessMode(ProcessMode mode) const
    {
        NativeScript::check(
            m_Script.m_Host.scene->set_node_process_mode(m_Script.m_Frame, m_Id, static_cast<uint64_t>(mode)));
    }

    inline int32_t SceneNode::processPriority() const
    {
        int32_t value = 0;
        NativeScript::check(m_Script.m_Host.scene->node_process_priority(m_Script.m_Frame, m_Id, &value));
        return value;
    }

    inline void SceneNode::setProcessPriority(int32_t priority) const
    {
        NativeScript::check(m_Script.m_Host.scene->set_node_process_priority(m_Script.m_Frame, m_Id, priority));
    }

    inline bool SceneNode::isInsideTree() const
    {
        uint64_t value = 0;
        NativeScript::check(m_Script.m_Host.scene->node_inside_tree(m_Script.m_Frame, m_Id, &value));
        return value != 0;
    }

    inline bool SceneNode::isQueuedForDeletion() const
    {
        uint64_t value = 0;
        NativeScript::check(m_Script.m_Host.scene->node_queued_for_deletion(m_Script.m_Frame, m_Id, &value));
        return value != 0;
    }

    inline bool SceneNode::inputEnabled() const
    {
        uint64_t value = 0;
        NativeScript::check(m_Script.m_Host.scene->node_input_enabled(m_Script.m_Frame, m_Id, &value));
        return value != 0;
    }

    inline void SceneNode::setInputEnabled(bool enabled) const
    {
        NativeScript::check(m_Script.m_Host.scene->set_node_input_enabled(m_Script.m_Frame, m_Id, enabled ? 1 : 0));
    }

    inline bool SceneNode::unhandledInputEnabled() const
    {
        uint64_t value = 0;
        NativeScript::check(m_Script.m_Host.scene->node_unhandled_input_enabled(m_Script.m_Frame, m_Id, &value));
        return value != 0;
    }

    inline void SceneNode::setUnhandledInputEnabled(bool enabled) const
    {
        NativeScript::check(
            m_Script.m_Host.scene->set_node_unhandled_input_enabled(m_Script.m_Frame, m_Id, enabled ? 1 : 0));
    }

    inline void SceneNode::queueFree() const
    {
        NativeScript::check(m_Script.m_Host.scene->queue_free(m_Script.m_Frame, m_Id));
    }

    inline MaterialParameters SceneMaterial::parameters() const
    {
        MaterialParameters value {};
        NativeScript::check(m_Script.m_Host.scene->material_parameters(m_Script.m_Frame, m_Id, &value));
        return value;
    }

    inline void SceneMaterial::setParameters(MaterialParameters parameters) const
    {
        NativeScript::check(m_Script.m_Host.scene->set_material_parameters(m_Script.m_Frame, m_Id, parameters));
    }

    inline std::string_view SceneMaterial::name() const
    {
        const char* data = nullptr;
        uint64_t    size = 0;
        NativeScript::check(m_Script.m_Host.scene->material_name(m_Script.m_Frame, m_Id, &data, &size));
        return {data, size};
    }

    inline std::optional<SceneMaterial> SceneNode::material(uint64_t slot) const
    {
        uint64_t id = 0;
        NativeScript::check(m_Script.m_Host.scene->mesh_material(m_Script.m_Frame, m_Id, slot, &id));
        if (id == 0)
        {
            return std::nullopt;
        }
        return SceneMaterial(m_Script, id);
    }

    inline void SceneNode::setMaterial(uint64_t slot, const SceneMaterial& material) const
    {
        NativeScript::check(m_Script.m_Host.scene->set_mesh_material(m_Script.m_Frame, m_Id, slot, material.m_Id));
    }

    inline void SceneNode::clearMaterial(uint64_t slot) const
    {
        NativeScript::check(m_Script.m_Host.scene->set_mesh_material(m_Script.m_Frame, m_Id, slot, 0));
    }

    inline Float3 SceneNode::position() const
    {
        VultraSceneTranslation value {};
        NativeScript::check(m_Script.m_Host.scene->node_translation(m_Script.m_Frame, m_Id, &value));
        return {value.x, value.y, value.z};
    }

    inline void SceneNode::setPosition(Float3 position) const
    {
        NativeScript::check(
            m_Script.m_Host.scene->set_node_translation(m_Script.m_Frame, m_Id, {position.x, position.y, position.z}));
    }

    inline std::string_view SceneNode::typeName() const
    {
        const char* data = nullptr;
        uint64_t    size = 0;
        NativeScript::check(m_Script.m_Host.scene->node_type(m_Script.m_Frame, m_Id, &data, &size));
        return {data, size};
    }

    inline std::optional<SceneNode> SceneNode::parent() const
    {
        uint64_t id = 0;
        NativeScript::check(m_Script.m_Host.scene->node_parent_id(m_Script.m_Frame, m_Id, &id));
        return id ? std::optional<SceneNode>(SceneNode(m_Script, id)) : std::nullopt;
    }

    inline std::optional<SceneNode> SceneNode::owner() const
    {
        uint64_t id = 0;
        NativeScript::check(m_Script.m_Host.scene->node_owner_id(m_Script.m_Frame, m_Id, &id));
        return id ? std::optional<SceneNode>(SceneNode(m_Script, id)) : std::nullopt;
    }

    inline void SceneNode::setOwner(std::optional<SceneNode> owner) const
    {
        NativeScript::check(m_Script.m_Host.scene->set_node_owner(m_Script.m_Frame, m_Id, owner ? owner->m_Id : 0));
    }

    inline SceneNode SceneNode::createChild(std::string_view name) const
    {
        uint64_t id = 0;
        NativeScript::check(m_Script.m_Host.scene->create_node(m_Script.m_Frame, m_Id, name.data(), name.size(), &id));
        return {m_Script, id};
    }

    inline SceneNode SceneNode::createChild3D(std::string_view name) const
    {
        uint64_t id = 0;
        NativeScript::check(
            m_Script.m_Host.scene->create_node3d(m_Script.m_Frame, m_Id, name.data(), name.size(), &id));
        return {m_Script, id};
    }

    inline SceneNode SceneNode::createCameraChild(std::string_view name) const
    {
        uint64_t id = 0;
        NativeScript::check(
            m_Script.m_Host.scene->create_camera(m_Script.m_Frame, m_Id, name.data(), name.size(), &id));
        return {m_Script, id};
    }

    inline SceneNode SceneNode::createLightChild(std::string_view name, LightKind kind) const
    {
        uint64_t id = 0;
        NativeScript::check(
            m_Script.m_Host.scene->create_light(m_Script.m_Frame, m_Id, name.data(), name.size(), uint64_t(kind), &id));
        return {m_Script, id};
    }

    inline SceneNode SceneNode::createEnvironmentChild(std::string_view name) const
    {
        uint64_t id = 0;
        NativeScript::check(
            m_Script.m_Host.scene->create_environment(m_Script.m_Frame, m_Id, name.data(), name.size(), &id));
        return {m_Script, id};
    }

    inline EnvironmentSettings SceneNode::environmentSettings() const
    {
        EnvironmentSettings value {};
        NativeScript::check(m_Script.m_Host.scene->environment_settings(m_Script.m_Frame, m_Id, &value));
        return value;
    }

    inline void SceneNode::setEnvironmentSettings(EnvironmentSettings settings) const
    {
        NativeScript::check(m_Script.m_Host.scene->set_environment_settings(m_Script.m_Frame, m_Id, settings));
    }

    inline void SceneNode::setEnvironmentAsset(std::string_view assetId) const
    {
        NativeScript::check(
            m_Script.m_Host.scene->set_environment_asset(m_Script.m_Frame, m_Id, assetId.data(), assetId.size()));
    }

    inline void SceneNode::makeEnvironmentCurrent() const
    {
        NativeScript::check(m_Script.m_Host.scene->set_current_environment(m_Script.m_Frame, m_Id));
    }

    inline CameraSettings SceneNode::cameraSettings() const
    {
        CameraSettings settings {};
        NativeScript::check(m_Script.m_Host.scene->camera_settings(m_Script.m_Frame, m_Id, &settings));
        return settings;
    }

    inline void SceneNode::setCameraSettings(CameraSettings settings) const
    {
        NativeScript::check(m_Script.m_Host.scene->set_camera_settings(m_Script.m_Frame, m_Id, settings));
    }

    inline void SceneNode::makeCurrent() const
    {
        NativeScript::check(m_Script.m_Host.scene->set_current_camera(m_Script.m_Frame, m_Id));
    }

    inline LightKind SceneNode::lightKind() const
    {
        uint64_t kind = 0;
        NativeScript::check(m_Script.m_Host.scene->light_kind(m_Script.m_Frame, m_Id, &kind));
        return LightKind(kind);
    }

    inline LightSettings SceneNode::lightSettings() const
    {
        LightSettings settings {};
        NativeScript::check(m_Script.m_Host.scene->light_settings(m_Script.m_Frame, m_Id, &settings));
        return settings;
    }

    inline void SceneNode::setLightSettings(LightSettings settings) const
    {
        NativeScript::check(m_Script.m_Host.scene->set_light_settings(m_Script.m_Frame, m_Id, settings));
    }

    inline SceneNode SceneNode::createMeshChild(std::string_view name, std::string_view assetId) const
    {
        uint64_t id = 0;
        NativeScript::check(
            m_Script.m_Host.scene
                ->create_mesh(m_Script.m_Frame, m_Id, name.data(), name.size(), assetId.data(), assetId.size(), &id));
        return {m_Script, id};
    }

    inline void SceneNode::reparent(SceneNode newParent, bool keepGlobalTransform) const
    {
        NativeScript::check(
            m_Script.m_Host.scene->reparent_node(m_Script.m_Frame, m_Id, newParent.m_Id, keepGlobalTransform ? 1 : 0));
    }

    inline SceneNode SceneNode::duplicateMesh(SceneNode parent) const
    {
        uint64_t id = 0;
        NativeScript::check(m_Script.m_Host.scene->duplicate_mesh(m_Script.m_Frame, m_Id, parent.m_Id, &id));
        return {m_Script, id};
    }

    inline void SceneNode::copyMeshModel(SceneNode source) const
    {
        NativeScript::check(m_Script.m_Host.scene->copy_mesh_model(m_Script.m_Frame, m_Id, source.m_Id));
    }

    inline void SceneNode::setMeshModel(std::string_view assetId) const
    {
        NativeScript::check(
            m_Script.m_Host.scene->set_mesh_model(m_Script.m_Frame, m_Id, assetId.data(), assetId.size()));
    }

    inline void SceneNode::remove() const
    {
        NativeScript::check(m_Script.m_Host.scene->remove_node(m_Script.m_Frame, m_Id));
    }

    inline SceneNode SceneNode::child(uint64_t index) const
    {
        uint64_t id = 0;
        NativeScript::check(m_Script.m_Host.scene->child_id(m_Script.m_Frame, m_Id, index, &id));
        return {m_Script, id};
    }

    inline uint64_t SceneNode::childCount() const
    {
        uint64_t count = 0;
        NativeScript::check(m_Script.m_Host.scene->child_count(m_Script.m_Frame, m_Id, &count));
        return count;
    }

    inline std::string_view SceneNode::name() const
    {
        const char* data = nullptr;
        uint64_t    size = 0;
        NativeScript::check(m_Script.m_Host.scene->node_name(m_Script.m_Frame, m_Id, &data, &size));
        return {data, size};
    }

    inline std::string NativeScript::readPropertyJson(uint64_t id, std::string_view name) const
    {
        uint64_t size = 0;
        check(m_Host.scene->read_property(m_Frame, id, name.data(), name.size(), nullptr, 0, &size));
        std::string result(size, '\0');
        check(m_Host.scene->read_property(m_Frame, id, name.data(), name.size(), result.data(), result.size(), &size));
        return result;
    }

    inline void NativeScript::writePropertyJson(uint64_t id, std::string_view name, std::string_view json) const
    {
        check(m_Host.scene->write_property(m_Frame, id, name.data(), name.size(), json.data(), json.size()));
    }

    inline std::string SceneNode::propertyJson(std::string_view name) const
    {
        return m_Script.readPropertyJson(m_Id, name);
    }

    inline std::string SceneNode::propertyListJson() const
    {
        uint64_t size = 0;
        NativeScript::check(m_Script.m_Host.scene->property_list(m_Script.m_Frame, m_Id, nullptr, 0, &size));
        std::string result(size, '\0');
        NativeScript::check(
            m_Script.m_Host.scene->property_list(m_Script.m_Frame, m_Id, result.data(), result.size(), &size));
        return result;
    }

    inline void SceneNode::setPropertyJson(std::string_view name, std::string_view json) const
    {
        m_Script.writePropertyJson(m_Id, name, json);
    }

    inline std::string SceneResource::propertyJson(std::string_view name) const
    {
        return m_Script.readPropertyJson(m_Id, name);
    }

    inline std::vector<std::string> SceneNode::components() const
    {
        uint64_t count = 0;
        NativeScript::check(m_Script.m_Host.scene->component_count(m_Script.m_Frame, m_Id, &count));
        std::vector<std::string> result;
        result.reserve(size_t(count));
        for (uint64_t index = 0; index < count; ++index)
        {
            const char* name = nullptr;
            uint64_t    size = 0;
            NativeScript::check(m_Script.m_Host.scene->component_name(m_Script.m_Frame, m_Id, index, &name, &size));
            result.emplace_back(name, size);
        }
        return result;
    }

    inline std::string SceneNode::componentPropertyJson(std::string_view component, std::string_view name) const
    {
        const auto& api  = *m_Script.m_Host.scene;
        uint64_t    size = 0;
        NativeScript::check(api.read_component_property(m_Script.m_Frame,
                                                        m_Id,
                                                        component.data(),
                                                        component.size(),
                                                        name.data(),
                                                        name.size(),
                                                        nullptr,
                                                        0,
                                                        &size));
        std::string result(size, '\0');
        NativeScript::check(api.read_component_property(m_Script.m_Frame,
                                                        m_Id,
                                                        component.data(),
                                                        component.size(),
                                                        name.data(),
                                                        name.size(),
                                                        result.data(),
                                                        result.size(),
                                                        &size));
        return result;
    }

    inline void
    SceneNode::setComponentPropertyJson(std::string_view component, std::string_view name, std::string_view json) const
    {
        NativeScript::check(m_Script.m_Host.scene->write_component_property(m_Script.m_Frame,
                                                                            m_Id,
                                                                            component.data(),
                                                                            component.size(),
                                                                            name.data(),
                                                                            name.size(),
                                                                            json.data(),
                                                                            json.size()));
    }

    inline void SceneResource::setPropertyJson(std::string_view name, std::string_view json) const
    {
        m_Script.writePropertyJson(m_Id, name, json);
    }

    inline std::string_view SceneResource::typeName() const
    {
        const char* data = nullptr;
        uint64_t    size = 0;
        NativeScript::check(m_Script.m_Host.scene->object_type(m_Script.m_Frame, m_Id, &data, &size));
        return {data, size};
    }

    inline void SceneResource::remove() const
    {
        NativeScript::check(m_Script.m_Host.scene->remove_resource(m_Script.m_Frame, m_Id));
    }

    inline SceneResource NativeScript::createResource(std::string_view type, std::string_view state) const
    {
        uint64_t id = 0;
        check(m_Host.scene->create_resource(m_Frame, type.data(), type.size(), state.data(), state.size(), &id));
        return {*this, id};
    }

    inline std::optional<SceneResource> NativeScript::findResource(std::string_view resourceId) const
    {
        uint64_t id = 0;
        check(m_Host.scene->find_resource(m_Frame, resourceId.data(), resourceId.size(), &id));
        return id ? std::optional<SceneResource>(SceneResource(*this, id)) : std::nullopt;
    }

    inline std::vector<std::string> NativeScript::typeNames() const
    {
        uint64_t count = 0;
        check(m_Host.scene->type_count(m_Frame, &count));
        std::vector<std::string> result;
        result.reserve(size_t(count));
        for (uint64_t index = 0; index < count; ++index)
        {
            const char* name = nullptr;
            uint64_t    size = 0;
            check(m_Host.scene->type_name(m_Frame, index, &name, &size));
            result.emplace_back(name, size);
        }
        return result;
    }

    inline std::string NativeScript::describeTypeJson(std::string_view type) const
    {
        uint64_t size = 0;
        check(m_Host.scene->describe_type(m_Frame, type.data(), type.size(), nullptr, 0, &size));
        std::string result(size, '\0');
        check(m_Host.scene->describe_type(m_Frame, type.data(), type.size(), result.data(), result.size(), &size));
        return result;
    }

    template<std::derived_from<NativeScript> T>
    NativeScriptClass scriptClass(std::string_view                  name,
                                  std::vector<NativeScriptProperty> properties = {},
                                  std::string_view                  baseType   = "Node")
    {
        NativeScriptClass result {std::string(name),
                                  nullptr,
                                  std::move(properties),
                                  std::string(baseType),
                                  typeid(T),
                                  {std::string(name)}};
        result.input = !std::is_same_v<decltype(&T::onInput), decltype(&NativeScript::onInput)>;
        result.unhandledInput =
            !std::is_same_v<decltype(&T::onUnhandledInput), decltype(&NativeScript::onUnhandledInput)>;
        if constexpr (!std::is_abstract_v<T>)
        {
            result.create = []() -> std::unique_ptr<NativeScript>
            {
                return std::make_unique<T>();
            };
        }
        return result;
    }

    template<std::derived_from<NativeScript> T, std::derived_from<NativeScript> Base>
        requires std::derived_from<T, Base> && (!std::same_as<T, Base>)
    NativeScriptClass
    scriptClass(std::string_view name, const NativeScriptClass& base, std::vector<NativeScriptProperty> properties = {})
    {
        if (base.cppType != typeid(Base) || base.classes.empty() || base.classes.front() != base.name)
        {
            throw std::invalid_argument("Native script descriptor does not describe the declared C++ base");
        }
        auto result = scriptClass<T>(name, base.properties, base.baseType);
        for (auto& property : properties)
        {
            if (std::ranges::find(result.properties, property.name, &NativeScriptProperty::name) !=
                result.properties.end())
            {
                throw std::invalid_argument("Native script field shadows an inherited export: " + property.name);
            }
            result.properties.push_back(std::move(property));
        }
        result.classes.insert(result.classes.end(), base.classes.begin(), base.classes.end());
        return result;
    }

    inline VultraStatus initializeNativeModule(const VultraHostApi*               host,
                                               VultraPluginApi*                   plugin,
                                               std::span<const NativeScriptClass> classes)
    {
        // Version 1 evolves by breaking changes; larger tables must not be treated as compatible SDK layouts.
        if (!host || !plugin || host->version != VULTRA_ABI_VERSION || host->struct_size != sizeof(VultraHostApi) ||
            (host->ui && (host->ui->version != VULTRA_ABI_VERSION || host->ui->struct_size != sizeof(VultraUiApi) ||
                          host->ui->fingerprint != VULTRA_UI_API_FINGERPRINT)) ||
            !host->scene || host->scene->version != VULTRA_ABI_VERSION ||
            host->scene->struct_size != sizeof(VultraSceneApi) ||
            host->scene->fingerprint != VULTRA_SCENE_API_FINGERPRINT || plugin->version != VULTRA_ABI_VERSION ||
            plugin->struct_size != sizeof(VultraPluginApi))
        {
            return VULTRA_STATUS_INVALID_ARGUMENT;
        }
        if (!plugin->user_data)
        {
            if (!host->scripts || host->scripts->version != VULTRA_ABI_VERSION ||
                host->scripts->struct_size != sizeof(VultraScriptRegistrationApi) || !host->scripts->register_class)
            {
                return VULTRA_STATUS_INVALID_ARGUMENT;
            }
            for (const auto& entry : classes)
            {
                if (host->scripts->register_class(host->scripts->user_data, entry.name.data(), entry.name.size()) !=
                    VULTRA_STATUS_OK)
                {
                    return VULTRA_STATUS_INVALID_ARGUMENT;
                }
            }
            plugin->update = [](void*, VultraSceneFrame, float)
            {
                return VULTRA_STATUS_OK;
            };
            plugin->on_gui = [](void*, VultraUiFrame)
            {
                return VULTRA_STATUS_OK;
            };
            plugin->stop = [](void*)
            {
                return VULTRA_STATUS_OK;
            };
            return VULTRA_STATUS_OK;
        }
        const auto* request = static_cast<const NativeScriptInit*>(plugin->user_data);
        if (request->nodeId == 0 || !request->typeName || request->typeNameSize == 0)
        {
            return VULTRA_STATUS_INVALID_ARGUMENT;
        }
        const std::string_view typeName(request->typeName, request->typeNameSize);
        for (const auto& entry : classes)
        {
            if (entry.name != typeName)
            {
                continue;
            }
            try
            {
                if (!entry.create)
                {
                    throw std::invalid_argument("Native script class has no factory");
                }
                auto script = entry.create();
                if (!script)
                {
                    throw std::invalid_argument("Native script factory returned null");
                }
                script->attach(*host, request->nodeId, entry);
                plugin->user_data  = script.release();
                plugin->update     = &NativeScript::update;
                plugin->on_gui     = &NativeScript::gui;
                plugin->stop       = &NativeScript::stop;
                plugin->on_scene   = &NativeScript::notify;
                plugin->properties = &scriptPropertyApi<NativeScript>();
                return VULTRA_STATUS_OK;
            }
            catch (const std::exception& error)
            {
                std::fprintf(stderr, "Native script load: %s\n", error.what());
                return VULTRA_STATUS_ERROR;
            }
        }
        std::fprintf(stderr, "Native script class not registered: %.*s\n", int(typeName.size()), typeName.data());
        return VULTRA_STATUS_INVALID_ARGUMENT;
    }
} // namespace vultra::scripting

#ifdef _WIN32
#define VULTRA_NATIVE_SCRIPT_EXPORT __declspec(dllexport)
#else
#define VULTRA_NATIVE_SCRIPT_EXPORT __attribute__((visibility("default")))
#endif

// A library may register several classes; a single-class script stays one line.
#define VULTRA_NATIVE_CLASS(ScriptType) vultra::scripting::scriptClass<ScriptType>(#ScriptType)
#define VULTRA_NATIVE_MODULE(...) \
    extern "C" VULTRA_NATIVE_SCRIPT_EXPORT VultraStatus vultra_plugin_init(const VultraHostApi* host, \
                                                                           VultraPluginApi*     plugin) \
    { \
        try \
        { \
            static const vultra::scripting::NativeScriptClass classes[] = {__VA_ARGS__}; \
            return vultra::scripting::initializeNativeModule(host, plugin, classes); \
        } \
        catch (const std::exception& error) \
        { \
            std::fprintf(stderr, "Native script declarations: %s\n", error.what()); \
            return VULTRA_STATUS_ERROR; \
        } \
    }
#define VULTRA_NATIVE_SCRIPT(ScriptType) VULTRA_NATIVE_MODULE(VULTRA_NATIVE_CLASS(ScriptType))
