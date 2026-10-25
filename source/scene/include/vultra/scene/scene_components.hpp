#pragma once

#include <vultra/core/base/api_annotations.hpp>
#include <vultra/core/base/property.hpp>
#include <vultra/scene/object.hpp>

#include <entt/entity/entity.hpp>
#include <glm/mat4x4.hpp>

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vultra
{
    class Node;
    class NodeBehavior;

    // Owns detached component state until a node is reattached; attached state remains solely in EnTT.
    using SceneComponentRestore = std::move_only_function<void(entt::registry&, entt::entity)>;

    // Borrowed with its behavior; field storage and callbacks belong to the optional language host.
    struct NodePropertyBinding
    {
        std::span<const PropertySchema> properties;
        void*                           state                        = nullptr;
        PropertyValue (*read)(void*, std::string_view)               = nullptr;
        void (*write)(void*, std::string_view, const PropertyValue&) = nullptr;
        std::span<const PropertyClassId> classes {};
    };

    struct VULTRA_REFERENCE("node") NodeId
    {
        StableId    value;
        friend bool operator==(const NodeId&, const NodeId&) = default;
    };

    enum class ProcessMode
    {
        eInherit,
        ePausable,
        eWhenPaused,
        eAlways,
        eDisabled
    };

    enum class NodeKind
    {
        eNode,
        eNode3D,
        eMeshInstance,
        eCamera,
        eLight,
        eEnvironment
    };

    // Runtime-only entities have no Node facade; persistent IDs never contain an EnTT slot or generation.
    struct VULTRA_REFLECT_COMPONENT SceneNode
    {
        ObjectId id;
        Node*    node = nullptr;
        NodeId   persistentId;
        VULTRA_PROPERTY("label=Name")
        std::string name {};
        size_t      treeOrder = 0;
    };

    struct VULTRA_REFLECT_COMPONENT SceneHierarchy
    {
        entt::entity              parent = entt::null;
        entt::entity              owner  = entt::null;
        std::vector<entt::entity> children;
        uint8_t                   traversalState = 0; // Derived scratch for cycle checks and parent-first ordering.
    };

    // Callback instances are borrowed from their host. All execution state belongs to the world.
    struct VULTRA_REFLECT_COMPONENT SceneProcess
    {
        VULTRA_PROPERTY("label=Process mode;options=Inherit|Pausable|WhenPaused|Always|Disabled")
        ProcessMode mode = ProcessMode::eInherit;
        VULTRA_PROPERTY("label=Priority;min=-1000;max=1000;widget=drag;speed=1")
        int32_t                    priority              = 0;
        NodeBehavior*              behavior              = nullptr;
        const NodePropertyBinding* properties            = nullptr;
        bool                       insideTree            = false;
        bool                       ready                 = false;
        bool                       behaviorReady         = false;
        bool                       queuedForDeletion     = false;
        bool                       blocked               = false;
        bool                       inputEnabled          = true;
        bool                       unhandledInputEnabled = true;
    };

    struct VULTRA_REFLECT_COMPONENT SceneTransform
    {
        VULTRA_PROPERTY("label=Local transform")
        glm::mat4   local {1};
        glm::mat4   global {1};
        static void validate(const glm::mat4& transform);
    };

    struct SceneTransformUpdate
    {
        ObjectId  node;
        glm::mat4 local;
    };

    enum class ScenePhase
    {
        ePhysics,
        eProcess
    };

    // World-local callback identity. World moves preserve it; replacing the world retires it.
    struct SceneSystemId
    {
        ObjectId    world;
        uint64_t    value                                    = 0;
        friend bool operator==(SceneSystemId, SceneSystemId) = default;
    };
} // namespace vultra
