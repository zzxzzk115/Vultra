#pragma once

#include <vultra/api/native_plugin.h>
#include <vultra/api/scene_bridge.hpp>
#include <vultra/api/vultra_native_type.generated.h>
#include <vultra/scene/scene_types.hpp>

#include <entt/entity/registry.hpp>

#include <string>
#include <vector>

namespace vultra
{
    struct NativeTypeRecord
    {
        std::string                 name;
        std::vector<PropertySchema> fields;
        std::vector<PropertyInfo>   properties;
        ObjectTypeInfo              type {};
        SceneTypeLifetime           lifetime;
        VultraNativeTypeApi         api {};
        void*                       factory      = nullptr;
        entt::id_type               storage      = 0;
        const SceneNodeType*        nodeBase     = nullptr;
        const SceneResourceType*    resourceBase = nullptr;
        SceneNodeType               node {type, {}, {}};
        SceneResourceType           resource {type, {}, {}};
        SceneComponentType          component {type, {}, {}};
    };

    // Exactly one owner, either a detached facade/resource or its named EnTT pool. Never mirrored per frame.
    class NativeTypeState
    {
    public:
        NativeTypeState(NativeTypeRecord& type, std::string_view json);
        ~NativeTypeState();
        NativeTypeState(NativeTypeState&& other) noexcept;
        NativeTypeState& operator=(NativeTypeState&& other) noexcept;
        NativeTypeState(const NativeTypeState&)            = delete;
        NativeTypeState& operator=(const NativeTypeState&) = delete;

        NativeTypeRecord& type() const;
        PropertyValue     read(const PropertySchema& property) const;
        void              write(const PropertySchema& property, const PropertyValue& value);
        std::string       serialize() const;
        void              notify(SceneTree& scene, ObjectId id, VultraSceneEvent event, double deltaSeconds = 0);

    private:
        void              reset() noexcept;
        NativeTypeRecord* m_Type     = nullptr;
        void*             m_Instance = nullptr;
        SceneAccess       m_Access;
    };

    entt::registry::storage_for_type<NativeTypeState>& nativeTypeStorage(entt::registry& registry, entt::id_type id);
    const NativeTypeState* nativeTypeState(const entt::registry& registry, entt::entity entity, entt::id_type id);
    void                   bindNativeNode(NativeTypeRecord& record);
    void                   bindNativeResource(NativeTypeRecord& record);
    void                   bindNativeComponent(NativeTypeRecord& record);
} // namespace vultra
