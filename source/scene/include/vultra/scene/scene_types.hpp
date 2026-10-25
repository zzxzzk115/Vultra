#pragma once

#include <vultra/core/base/property.hpp>
#include <vultra/scene/node.hpp>
#include <vultra/scene/resource.hpp>

#include <entt/entity/fwd.hpp>

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vultra
{
    class SceneTree;

    // Counts borrows, not ownership. A project must keep extension records/code alive until both reach zero.
    struct SceneTypeLifetime
    {
        size_t catalogs  = 0;
        size_t instances = 0;
    };

    // State contains type-specific JSON fields. Common hierarchy/spatial/execution fields belong to SceneTree.
    struct SceneNodeType
    {
        const ObjectTypeInfo&                                                                     type;
        std::function<std::unique_ptr<Node>(std::string name, NodeId id, std::string_view state)> create;
        std::function<std::string(const Node& node)>                                              serialize;
        SceneTypeLifetime*                                                                        lifetime = nullptr;
        const ObjectTypeInfo* extensionBase                                                                = nullptr;
    };

    struct SceneResourceType
    {
        const ObjectTypeInfo&                                                           type;
        std::function<std::unique_ptr<Resource>(ResourceId id, std::string_view state)> create;
        std::function<std::string(const Resource& resource)>                            serialize;
        SceneTypeLifetime*                                                              lifetime      = nullptr;
        const ObjectTypeInfo*                                                           extensionBase = nullptr;
    };

    struct SceneComponentType
    {
        const ObjectTypeInfo&                                                           type;
        std::function<const void*(const entt::registry& registry, entt::entity entity)> get;
        // Validate a candidate before publishing through registry.patch; registrations own their revision policy.
        std::function<
            void(SceneTree& scene, entt::entity entity, const PropertyInfo& property, const PropertyValue& value)>
                           write;
        SceneTypeLifetime* lifetime = nullptr;
        // Only explicitly constructible extension components expose structural changes through the ABI.
        std::function<void(SceneTree& scene, entt::entity entity, std::string_view state)>  create;
        std::function<void(SceneTree& scene, entt::entity entity)>                          remove;
        std::function<std::string(const entt::registry& registry, entt::entity entity)>     serialize;
        std::function<SceneComponentRestore(entt::registry& registry, entt::entity entity)> take;
    };

    // Records/code are borrowed and must outlive worlds, created C++ instances and their EnTT component pools.
    class SceneTypeCatalog
    {
    public:
        SceneTypeCatalog();
        ~SceneTypeCatalog();
        SceneTypeCatalog(const SceneTypeCatalog& other);
        SceneTypeCatalog& operator=(const SceneTypeCatalog& other);
        SceneTypeCatalog(SceneTypeCatalog&& other) noexcept;
        SceneTypeCatalog&                          operator=(SceneTypeCatalog&& other) noexcept;
        void                                       add(const SceneNodeType& type);
        void                                       remove(const SceneNodeType& type);
        void                                       add(const SceneResourceType& type);
        void                                       remove(const SceneResourceType& type);
        void                                       add(const SceneComponentType& type);
        void                                       remove(const SceneComponentType& type);
        const SceneNodeType&                       nodeType(std::string_view name) const;
        const SceneResourceType&                   resourceType(std::string_view name) const;
        const SceneComponentType&                  componentType(std::string_view name) const;
        std::span<const SceneComponentType* const> components() const;
        const ObjectTypeCatalog&                   objects() const;
        std::unique_ptr<Node> create(std::string_view type, std::string name, NodeId id, std::string_view state) const;
        std::string           serialize(const Node& node) const;
        std::unique_ptr<Resource> createResource(std::string_view type, ResourceId id, std::string_view state) const;
        std::string               serialize(const Resource& resource) const;

    private:
        void                                   borrow(SceneTypeLifetime* lifetime);
        void                                   release(SceneTypeLifetime* lifetime);
        void                                   swap(SceneTypeCatalog& other) noexcept;
        void                                   addBuiltinResources();
        void                                   addBuiltinComponents();
        ObjectTypeCatalog                      m_Objects;
        std::vector<const SceneNodeType*>      m_Nodes;
        std::vector<const SceneResourceType*>  m_Resources;
        std::vector<const SceneComponentType*> m_Components;
        std::vector<SceneTypeLifetime*>        m_Lifetimes;
    };
} // namespace vultra
