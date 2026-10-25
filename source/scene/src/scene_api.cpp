#include <vultra/scene/render_nodes.hpp>
#include <vultra/scene/scene_api.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace vultra
{
    SceneInputData sceneInputEvent(const SceneTree& scene)
    {
        SceneInputData data {};
        data.serial = scene.inputEventSerial();
        std::visit(
            [&](const auto& value)
            {
                using Event = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Event, KeyInput>)
                {
                    data.kind      = uint32_t(InputEventKind::eKey);
                    data.code      = uint32_t(value.key);
                    data.action    = uint32_t(value.action);
                    data.modifiers = value.modifiers;
                }
                else if constexpr (std::is_same_v<Event, MouseButtonInput>)
                {
                    data.kind      = uint32_t(InputEventKind::eMouseButton);
                    data.code      = uint32_t(value.button);
                    data.action    = uint32_t(value.held ? InputAction::ePress : InputAction::eRelease);
                    data.modifiers = value.modifiers;
                    data.x         = value.position.x;
                    data.y         = value.position.y;
                }
                else if constexpr (std::is_same_v<Event, MouseMotionInput> || std::is_same_v<Event, MouseScrollInput>)
                {
                    data.kind      = uint32_t(std::is_same_v<Event, MouseMotionInput> ? InputEventKind::eMouseMotion :
                                                                                        InputEventKind::eMouseScroll);
                    data.modifiers = value.modifiers;
                    data.x         = value.position.x;
                    data.y         = value.position.y;
                    data.deltaX    = value.delta.x;
                    data.deltaY    = value.delta.y;
                }
                else if constexpr (std::is_same_v<Event, TextInput>)
                {
                    data.kind = uint32_t(InputEventKind::eText);
                }
                else if constexpr (std::is_same_v<Event, FocusInput>)
                {
                    data.kind   = uint32_t(InputEventKind::eFocus);
                    data.action = uint32_t(value.focused ? InputAction::ePress : InputAction::eRelease);
                }
            },
            scene.inputEvent(data.serial).value);
        return data;
    }

    std::string_view sceneInputText(const SceneTree& scene, uint64_t serial)
    {
        const auto* text = std::get_if<TextInput>(&scene.inputEvent(serial).value);
        return text ? std::string_view(text->text) : std::string_view {};
    }

    uint64_t sceneInputActionState(const SceneTree& scene, uint64_t serial, std::string_view name)
    {
        const auto action = scene.input().eventAction(name, scene.inputEvent(serial));
        return uint64_t(action.held) | (uint64_t(action.pressed) << 1) | (uint64_t(action.released) << 2);
    }

    void sceneSetInputHandled(SceneTree& scene, uint64_t serial)
    {
        scene.setInputHandled(serial);
    }

    uint64_t sceneRandomSeed(const SceneTree& scene)
    {
        return scene.randomSeed();
    }

    uint64_t sceneRandomUInt(SceneTree& scene, std::string_view stream)
    {
        return scene.randomUInt(stream);
    }

    uint64_t sceneRandomBoundedUInt(SceneTree& scene, std::string_view stream, uint64_t bound)
    {
        if (bound > std::numeric_limits<uint32_t>::max())
        {
            throw std::invalid_argument("Random integer bound exceeds 32 bits");
        }
        return scene.randomUInt(stream, uint32_t(bound));
    }

    double sceneRandomUnit(SceneTree& scene, std::string_view stream)
    {
        return scene.randomUnit(stream);
    }

    std::string sceneRandomState(const SceneTree& scene)
    {
        return scene.randomState();
    }

    void sceneSetRandomSeed(SceneTree& scene, uint64_t seed)
    {
        if (seed > std::numeric_limits<uint32_t>::max())
        {
            throw std::invalid_argument("World random seed exceeds 32 bits");
        }
        scene.setRandomSeed(uint32_t(seed));
    }

    void sceneRestoreRandomState(SceneTree& scene, std::string_view json)
    {
        scene.restoreRandomState(json);
    }

    uint64_t sceneActionState(const SceneTree& scene, std::string_view name)
    {
        const auto action = scene.action(name);
        return uint64_t(action.held) | (uint64_t(action.pressed) << 1) | (uint64_t(action.released) << 2);
    }

    namespace
    {
        Node& requireNode(SceneTree& scene, ObjectId id)
        {
            auto* node = scene.find(id);
            if (!node)
            {
                throw std::invalid_argument("ObjectId does not belong to this scene");
            }
            return *node;
        }

        const Node& requireNode(const SceneTree& scene, ObjectId id)
        {
            const auto* node = scene.find(id);
            if (!node)
            {
                throw std::invalid_argument("ObjectId does not belong to this scene");
            }
            return *node;
        }

        Node3D& requireNode3D(SceneTree& scene, ObjectId id)
        {
            auto* node = dynamic_cast<Node3D*>(&requireNode(scene, id));
            if (!node)
            {
                throw std::invalid_argument("Scene node is not a Node3D");
            }
            return *node;
        }

        const Node3D& requireNode3D(const SceneTree& scene, ObjectId id)
        {
            const auto* node = dynamic_cast<const Node3D*>(&requireNode(scene, id));
            if (!node)
            {
                throw std::invalid_argument("Scene node is not a Node3D");
            }
            return *node;
        }

        AssetId requireModelAsset(const ProjectManifest& project, std::string_view text)
        {
            const auto parsed = StableId::parse(text);
            if (!parsed)
            {
                throw std::invalid_argument("Invalid model asset ID");
            }
            const AssetId id {*parsed};
            const auto    extension = project.asset(id).path.extension();
            if (extension != ".gltf" && extension != ".glb" && extension != ".obj" && extension != ".fbx")
            {
                throw std::invalid_argument("Asset is not an importable model");
            }
            return id;
        }

        MeshInstance3D& requireMesh(SceneTree& scene, ObjectId id)
        {
            auto& node = requireNode(scene, id);
            if (node.kind() != NodeKind::eMeshInstance)
            {
                throw std::invalid_argument("ObjectId is not a mesh instance");
            }
            return static_cast<MeshInstance3D&>(node);
        }

        const MeshInstance3D& requireMesh(const SceneTree& scene, ObjectId id)
        {
            const auto& node = requireNode(scene, id);
            if (node.kind() != NodeKind::eMeshInstance)
            {
                throw std::invalid_argument("ObjectId is not a mesh instance");
            }
            return static_cast<const MeshInstance3D&>(node);
        }

        MaterialResource& requireMaterial(const SceneTree& scene, ObjectId id)
        {
            auto* material = scene.findMaterial(id);
            if (!material)
            {
                throw std::invalid_argument("ObjectId is not a material in this scene");
            }
            return *material;
        }

        uint32_t materialSlot(uint64_t slot)
        {
            if (slot > std::numeric_limits<uint32_t>::max())
            {
                throw std::invalid_argument("Material slot exceeds the model index range");
            }
            return uint32_t(slot);
        }
    } // namespace

    ObjectId sceneRootId(const SceneTree& scene)
    {
        return scene.root().id();
    }

    uint64_t sceneChildCount(const SceneTree& scene, ObjectId parent)
    {
        return requireNode(scene, parent).children().size();
    }

    ObjectId sceneChildId(const SceneTree& scene, ObjectId parent, uint64_t index)
    {
        const auto& children = requireNode(scene, parent).children();
        if (index >= children.size())
        {
            throw std::invalid_argument("Scene child index is out of range");
        }
        return children[size_t(index)]->id();
    }

    std::string_view sceneNodeName(const SceneTree& scene, ObjectId node)
    {
        return requireNode(scene, node).name();
    }

    std::string_view sceneNodeType(const SceneTree& scene, ObjectId node)
    {
        return requireNode(scene, node).typeName();
    }

    uint64_t sceneTypeCount(const SceneTree& scene)
    {
        return scene.types().objects().types().size();
    }

    std::string_view sceneTypeName(const SceneTree& scene, uint64_t index)
    {
        const auto types = scene.types().objects().types();
        if (index >= types.size())
        {
            throw std::invalid_argument("Scene type index is out of range");
        }
        return types[size_t(index)]->name;
    }

    std::string sceneDescribeType(const SceneTree& scene, std::string_view type)
    {
        return serializeTypeDescription(scene.types().objects().type(type));
    }

    std::string scenePropertyList(const SceneTree& scene, ObjectId object)
    {
        auto properties = nlohmann::json::array();
        for (const auto& property : scene.propertyList(object))
        {
            if (hasPropertyFlag(property.flags, PropertyFlags::eBind))
            {
                properties.push_back(nlohmann::json::parse(serializePropertySchema(property)).at("property"));
            }
        }
        auto classes = nlohmann::json::array();
        for (const auto& type : scene.behaviorClasses(object))
        {
            classes.push_back({{"module", type.module}, {"name", type.name}});
        }
        return nlohmann::json {{"version", 1}, {"properties", std::move(properties)}, {"classes", std::move(classes)}}
            .dump();
    }

    uint64_t sceneComponentCount(const SceneTree& scene, ObjectId id)
    {
        const auto entity = scene.entity(id);
        if (entity == entt::null)
        {
            throw std::invalid_argument("Component target does not belong to this world");
        }
        return std::ranges::count_if(scene.types().components(),
                                     [&](const auto* record)
                                     {
                                         return record->get(scene.registry(), entity) != nullptr;
                                     });
    }

    std::string_view sceneComponentName(const SceneTree& scene, ObjectId id, uint64_t index)
    {
        const auto entity = scene.entity(id);
        if (entity == entt::null)
        {
            throw std::invalid_argument("Component target does not belong to this world");
        }
        for (const auto* record : scene.types().components())
        {
            if (record->get(scene.registry(), entity))
            {
                if (index == 0)
                {
                    return record->type.name;
                }
                --index;
            }
        }
        throw std::invalid_argument("Component index is out of range");
    }

    std::string
    sceneReadComponentProperty(const SceneTree& scene, ObjectId id, std::string_view component, std::string_view name)
    {
        const auto& property = scene.componentType(id, component).type.property(name);
        if (!hasPropertyFlag(property.flags, PropertyFlags::eBind))
        {
            throw std::invalid_argument("Component property is not exposed to bindings");
        }
        return serializeProperty(property, scene.readComponentProperty(id, component, name));
    }

    void sceneWriteComponentProperty(SceneTree&             scene,
                                     const ProjectManifest* project,
                                     ObjectId               id,
                                     std::string_view       component,
                                     std::string_view       name,
                                     std::string_view       json)
    {
        const auto& property = scene.componentType(id, component).type.property(name);
        if (!hasPropertyFlag(property.flags, PropertyFlags::eBind))
        {
            throw std::invalid_argument("Component property is not exposed to bindings");
        }
        scene.writeComponentProperty(id, component, name, deserializeProperty(property, json), project);
    }

    std::string sceneReadProperty(const SceneTree& scene, ObjectId node, std::string_view property)
    {
        const auto& info = scene.propertySchema(node, property);
        if (!hasPropertyFlag(info.flags, PropertyFlags::eBind))
        {
            throw std::invalid_argument("Property is not exposed to bindings");
        }
        return serializeProperty(info, scene.readProperty(node, property));
    }

    void sceneWriteProperty(SceneTree&             scene,
                            const ProjectManifest* project,
                            ObjectId               node,
                            std::string_view       property,
                            std::string_view       json)
    {
        const auto& info = scene.propertySchema(node, property);
        if (!hasPropertyFlag(info.flags, PropertyFlags::eBind))
        {
            throw std::invalid_argument("Property is not exposed to bindings");
        }
        scene.writeProperty(node, property, deserializeProperty(info, json), project);
    }

    ObjectId sceneNodeParentId(const SceneTree& scene, ObjectId node)
    {
        const auto* parent = requireNode(scene, node).parent();
        return parent ? parent->id() : ObjectId {};
    }

    ObjectId sceneNodeOwnerId(const SceneTree& scene, ObjectId node)
    {
        const auto* owner = requireNode(scene, node).owner();
        return owner ? owner->id() : ObjectId {};
    }

    void sceneSetNodeOwner(SceneTree& scene, ObjectId node, ObjectId owner)
    {
        requireNode(scene, node).setOwner(owner.value ? &requireNode(scene, owner) : nullptr);
    }

    SceneTranslation sceneNodeTranslation(const SceneTree& scene, ObjectId node)
    {
        const auto& transform = requireNode3D(scene, node).localTransform();
        return {transform[3].x, transform[3].y, transform[3].z};
    }

    uint64_t sceneNodeInsideTree(const SceneTree& scene, ObjectId node)
    {
        return requireNode(scene, node).isInsideTree();
    }

    uint64_t sceneNodeQueuedForDeletion(const SceneTree& scene, ObjectId node)
    {
        return requireNode(scene, node).isQueuedForDeletion();
    }

    uint64_t sceneNodeProcessMode(const SceneTree& scene, ObjectId node)
    {
        return static_cast<uint64_t>(requireNode(scene, node).processMode());
    }

    void sceneSetNodeProcessMode(SceneTree& scene, ObjectId node, uint64_t mode)
    {
        if (mode > static_cast<uint64_t>(ProcessMode::eDisabled))
        {
            throw std::invalid_argument("Invalid node process mode");
        }
        requireNode(scene, node).setProcessMode(static_cast<ProcessMode>(mode));
    }

    int32_t sceneNodeProcessPriority(const SceneTree& scene, ObjectId node)
    {
        return requireNode(scene, node).processPriority();
    }

    uint64_t sceneNodeInputEnabled(const SceneTree& scene, ObjectId node)
    {
        return requireNode(scene, node).inputEnabled();
    }

    void sceneSetNodeInputEnabled(SceneTree& scene, ObjectId node, uint64_t enabled)
    {
        if (enabled > 1)
        {
            throw std::invalid_argument("Input enabled must be zero or one");
        }
        requireNode(scene, node).setInputEnabled(enabled != 0);
    }

    uint64_t sceneNodeUnhandledInputEnabled(const SceneTree& scene, ObjectId node)
    {
        return requireNode(scene, node).unhandledInputEnabled();
    }

    void sceneSetNodeUnhandledInputEnabled(SceneTree& scene, ObjectId node, uint64_t enabled)
    {
        if (enabled > 1)
        {
            throw std::invalid_argument("Unhandled input enabled must be zero or one");
        }
        requireNode(scene, node).setUnhandledInputEnabled(enabled != 0);
    }

    void sceneSetNodeProcessPriority(SceneTree& scene, ObjectId node, int32_t priority)
    {
        requireNode(scene, node).setProcessPriority(priority);
    }

    void sceneQueueFree(SceneTree& scene, ObjectId node)
    {
        scene.queueFree(node);
    }

    uint64_t scenePaused(const SceneTree& scene)
    {
        return scene.paused();
    }

    void sceneSetPaused(SceneTree& scene, uint64_t paused)
    {
        if (paused > 1)
        {
            throw std::invalid_argument("Pause flag must be zero or one");
        }
        scene.setPaused(paused != 0);
    }

    uint64_t scenePhysicsFrame(const SceneTree& scene)
    {
        return scene.physicsFrame();
    }

    double sceneTimeScale(const SceneTree& scene)
    {
        return scene.timeScale();
    }

    void sceneSetTimeScale(SceneTree& scene, double scale)
    {
        scene.setTimeScale(scale);
    }

    void sceneSetNodeTranslation(SceneTree& scene, ObjectId node, SceneTranslation translation)
    {
        auto& target    = requireNode3D(scene, node);
        auto  transform = target.localTransform();
        transform[3].x  = translation.x;
        transform[3].y  = translation.y;
        transform[3].z  = translation.z;
        target.setLocalTransform(transform);
    }

    ObjectId sceneCreateNode(SceneTree& scene, ObjectId parent, std::string_view name)
    {
        return scene.addChild(requireNode(scene, parent), std::make_unique<Node>(std::string(name))).id();
    }

    ObjectId sceneCreateNode3D(SceneTree& scene, ObjectId parent, std::string_view name)
    {
        return scene.addChild(requireNode(scene, parent), std::make_unique<Node3D>(std::string(name))).id();
    }

    ObjectId sceneCreateMesh(SceneTree&             scene,
                             const ProjectManifest& project,
                             ObjectId               parent,
                             std::string_view       name,
                             std::string_view       assetId)
    {
        const auto model = requireModelAsset(project, assetId);
        return scene.addChild(requireNode(scene, parent), std::make_unique<MeshInstance3D>(std::string(name), model))
            .id();
    }

    void sceneReparentNode(SceneTree& scene, ObjectId node, ObjectId newParent, uint64_t keepGlobal)
    {
        if (keepGlobal > 1)
        {
            throw std::invalid_argument("Keep-global-transform flag must be zero or one");
        }
        if (scene.dispatching())
        {
            scene.deferReparent(node, newParent, keepGlobal != 0);
        }
        else
        {
            scene.reparent(requireNode(scene, node), requireNode(scene, newParent), keepGlobal != 0);
        }
    }

    ObjectId sceneDuplicateMesh(SceneTree& scene, ObjectId source, ObjectId parent)
    {
        const auto& mesh      = requireMesh(scene, source);
        auto        duplicate = std::make_unique<MeshInstance3D>(mesh.name() + " Copy", mesh.model());
        duplicate->setLocalTransform(mesh.localTransform());
        for (const auto& entry : mesh.materialOverrides())
        {
            duplicate->setMaterial(entry.slot, entry.material);
        }
        return scene.addChild(requireNode(scene, parent), std::move(duplicate)).id();
    }

    void sceneCopyMeshModel(SceneTree& scene, ObjectId target, ObjectId source)
    {
        const auto model = requireMesh(scene, source).model();
        requireMesh(scene, target).setModel(model);
    }

    void sceneSetMeshModel(SceneTree& scene, const ProjectManifest& project, ObjectId target, std::string_view assetId)
    {
        auto& mesh = requireMesh(scene, target);
        mesh.setModel(requireModelAsset(project, assetId));
    }

    void sceneRemoveNode(SceneTree& scene, ObjectId node)
    {
        if (scene.dispatching())
        {
            scene.queueFree(node);
        }
        else
        {
            scene.remove(requireNode(scene, node));
        }
    }

    ObjectId sceneCreateCamera(SceneTree& scene, ObjectId parent, std::string_view name)
    {
        return scene.addChild(requireNode(scene, parent), std::make_unique<Camera3D>(std::string(name))).id();
    }

    CameraSettings sceneCameraSettings(const SceneTree& scene, ObjectId node)
    {
        const auto& camera = requireNode(scene, node);
        if (camera.kind() != NodeKind::eCamera)
        {
            throw std::invalid_argument("ObjectId is not a camera node");
        }
        return static_cast<const Camera3D&>(camera).settings();
    }

    void sceneSetCameraSettings(SceneTree& scene, ObjectId node, CameraSettings settings)
    {
        auto& camera = requireNode(scene, node);
        if (camera.kind() != NodeKind::eCamera)
        {
            throw std::invalid_argument("ObjectId is not a camera node");
        }
        static_cast<Camera3D&>(camera).setSettings(settings);
    }

    ObjectId sceneCurrentCamera(const SceneTree& scene)
    {
        return scene.currentCamera();
    }

    void sceneSetCurrentCamera(SceneTree& scene, ObjectId node)
    {
        scene.setCurrentCamera(node);
    }

    ObjectId sceneCreateLight(SceneTree& scene, ObjectId parent, std::string_view name, uint64_t kind)
    {
        if (kind > uint64_t(RenderLightKind::eSpot))
        {
            throw std::invalid_argument("Unknown light kind");
        }
        return scene
            .addChild(requireNode(scene, parent), std::make_unique<Light3D>(std::string(name), RenderLightKind(kind)))
            .id();
    }

    uint64_t sceneLightKind(const SceneTree& scene, ObjectId node)
    {
        const auto& light = requireNode(scene, node);
        if (light.kind() != NodeKind::eLight)
        {
            throw std::invalid_argument("ObjectId is not a light node");
        }
        return uint64_t(static_cast<const Light3D&>(light).lightKind());
    }

    LightSettings sceneLightSettings(const SceneTree& scene, ObjectId node)
    {
        const auto& light = requireNode(scene, node);
        if (light.kind() != NodeKind::eLight)
        {
            throw std::invalid_argument("ObjectId is not a light node");
        }
        return static_cast<const Light3D&>(light).settings();
    }

    void sceneSetLightSettings(SceneTree& scene, ObjectId node, LightSettings settings)
    {
        auto& light = requireNode(scene, node);
        if (light.kind() != NodeKind::eLight)
        {
            throw std::invalid_argument("ObjectId is not a light node");
        }
        static_cast<Light3D&>(light).setSettings(settings);
    }

    ObjectId sceneCreateEnvironment(SceneTree& scene, ObjectId parent, std::string_view name)
    {
        return scene.addChild(requireNode(scene, parent), std::make_unique<WorldEnvironment>(std::string(name))).id();
    }

    EnvironmentSettings sceneEnvironmentSettings(const SceneTree& scene, ObjectId node)
    {
        const auto& environment = requireNode(scene, node);
        if (environment.kind() != NodeKind::eEnvironment)
        {
            throw std::invalid_argument("ObjectId is not an environment node");
        }
        return static_cast<const WorldEnvironment&>(environment).settings();
    }

    void sceneSetEnvironmentSettings(SceneTree& scene, ObjectId node, EnvironmentSettings settings)
    {
        auto& environment = requireNode(scene, node);
        if (environment.kind() != NodeKind::eEnvironment)
        {
            throw std::invalid_argument("ObjectId is not an environment node");
        }
        static_cast<WorldEnvironment&>(environment).setSettings(settings);
    }

    void
    sceneSetEnvironmentAsset(SceneTree& scene, const ProjectManifest& project, ObjectId node, std::string_view assetId)
    {
        auto& environment = requireNode(scene, node);
        if (environment.kind() != NodeKind::eEnvironment)
        {
            throw std::invalid_argument("ObjectId is not an environment node");
        }
        AssetId asset {};
        if (!assetId.empty())
        {
            const auto parsed = StableId::parse(assetId);
            if (!parsed || project.asset({*parsed}).path.extension() != ".hdr")
            {
                throw std::invalid_argument("Environment asset must identify a project Radiance HDR image");
            }
            asset = {*parsed};
        }
        static_cast<WorldEnvironment&>(environment).setRadianceAsset(asset);
    }

    ObjectId sceneCurrentEnvironment(const SceneTree& scene)
    {
        return scene.currentEnvironment();
    }

    void sceneSetCurrentEnvironment(SceneTree& scene, ObjectId node)
    {
        scene.setCurrentEnvironment(node);
    }

    std::string_view sceneObjectType(const SceneTree& scene, ObjectId object)
    {
        return scene.objectType(object).name;
    }

    std::string sceneObjectPersistentId(const SceneTree& scene, ObjectId object, std::string_view type)
    {
        if (!scene.objectType(object).isA(type))
        {
            throw std::invalid_argument("Reference target has the wrong native type");
        }
        if (const auto* node = scene.find(object))
        {
            return node->idInScene().value.toString();
        }
        if (const auto* resource = scene.findResource(object))
        {
            return resource->resourceId().value.toString();
        }
        throw std::invalid_argument("Reference target does not belong to this world or has been retired");
    }

    ObjectId sceneFindNode(const SceneTree& scene, std::string_view nodeId)
    {
        const auto parsed = StableId::parse(nodeId);
        if (!parsed || !parsed->valid())
        {
            throw std::invalid_argument("Invalid persistent node ID");
        }
        const auto* node = scene.find(NodeId {*parsed});
        return node ? node->id() : ObjectId {};
    }

    ObjectId sceneFindResource(const SceneTree& scene, std::string_view resourceId)
    {
        const auto parsed = StableId::parse(resourceId);
        if (!parsed)
        {
            throw std::invalid_argument("Invalid scene resource ID");
        }
        const auto* resource = scene.findResource(ResourceId {*parsed});
        return resource ? resource->id() : ObjectId {};
    }

    ObjectId sceneCreateNodeType(SceneTree&             scene,
                                 const ProjectManifest* project,
                                 ObjectId               parent,
                                 std::string_view       type,
                                 std::string_view       name,
                                 std::string_view       state)
    {
        auto* owner = scene.find(parent);
        if (!owner)
        {
            throw std::invalid_argument("Registered node parent does not belong to this world");
        }
        auto node = scene.types().create(type, std::string(name), {StableId::generate()}, state);
        for (const auto& property : scene.types().objects().type(type).properties)
        {
            scene.validatePropertyValue(property, property.read(dynamic_cast<const void*>(node.get())), project);
        }
        return scene.addChild(*owner, std::move(node)).id();
    }

    void sceneAddComponent(SceneTree& scene, ObjectId entity, std::string_view type, std::string_view state)
    {
        scene.addComponent(entity, type, state);
    }

    void sceneRemoveComponent(SceneTree& scene, ObjectId entity, std::string_view type)
    {
        scene.removeComponent(entity, type);
    }

    ObjectId sceneCreateResource(SceneTree& scene, std::string_view type, std::string_view state)
    {
        auto& resource = scene.addResource(scene.types().createResource(type, {StableId::generate()}, state));
        try
        {
            scene.validateReferences();
        }
        catch (...)
        {
            scene.removeResource(resource);
            throw;
        }
        return resource.id();
    }

    void sceneRemoveResource(SceneTree& scene, ObjectId id)
    {
        auto* resource = scene.findResource(id);
        if (!resource)
        {
            throw std::invalid_argument("Resource does not belong to this scene");
        }
        scene.removeResource(*resource);
    }

    ObjectId sceneCreateMaterial(SceneTree& scene, std::string_view name)
    {
        return scene.addMaterial(std::make_unique<MaterialResource>(std::string(name))).id();
    }

    std::string_view sceneMaterialName(const SceneTree& scene, ObjectId material)
    {
        return requireMaterial(scene, material).name();
    }

    MaterialParameters sceneMaterialParameters(const SceneTree& scene, ObjectId material)
    {
        return requireMaterial(scene, material).parameters();
    }

    void sceneSetMaterialParameters(SceneTree& scene, ObjectId material, MaterialParameters parameters)
    {
        requireMaterial(scene, material).setParameters(parameters);
    }

    void sceneRemoveMaterial(SceneTree& scene, ObjectId material)
    {
        scene.removeMaterial(requireMaterial(scene, material));
    }

    ObjectId sceneMeshMaterial(const SceneTree& scene, ObjectId node, uint64_t slot)
    {
        const auto index = materialSlot(slot);
        for (const auto& entry : requireMesh(scene, node).materialOverrides())
        {
            if (entry.slot == index)
            {
                auto* material = scene.findMaterial(entry.material);
                if (!material)
                {
                    throw std::invalid_argument("Mesh references a missing material resource");
                }
                return material->id();
            }
        }
        return {};
    }

    void sceneSetMeshMaterial(SceneTree& scene, ObjectId node, uint64_t slot, ObjectId material)
    {
        const auto index      = materialSlot(slot);
        const auto resourceId = material.value ? requireMaterial(scene, material).resourceId() : ResourceId {};
        requireMesh(scene, node).setMaterial(index, resourceId);
    }
} // namespace vultra
