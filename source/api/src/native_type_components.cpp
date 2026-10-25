#include "native_type_state.hpp"

#include <vultra/scene/scene_tree.hpp>

#include <stdexcept>

namespace vultra
{
    void bindNativeComponent(NativeTypeRecord& record)
    {
        for (const auto& field : record.fields)
        {
            PropertyInfo property;
            static_cast<PropertySchema&>(property) = field;
            property.get                           = [field](const void* object)
            {
                return static_cast<const NativeTypeState*>(object)->read(field);
            };
            property.set = [field](void* object, const PropertyValue& value)
            {
                static_cast<NativeTypeState*>(object)->write(field, value);
            };
            record.properties.push_back(std::move(property));
        }
        record.component.lifetime = &record.lifetime;
        record.component.get      = [&record](const entt::registry& registry, entt::entity entity) -> const void*
        {
            return nativeTypeState(registry, entity, record.storage);
        };
        record.component.write =
            [&record](SceneTree& scene, entt::entity entity, const PropertyInfo& property, const PropertyValue& value)
        {
            auto& storage = nativeTypeStorage(scene.registry(), record.storage);
            // The module validates its semantic candidate before committing. Notify only after a successful write.
            property.write(&storage.get(entity), value);
            storage.patch(entity);
            scene.markChanged(SceneChange::eMetadata);
        };
        record.component.create = [&record](SceneTree& scene, entt::entity entity, std::string_view json)
        {
            NativeTypeState state(record, json);
            for (const auto& field : record.fields)
            {
                scene.validatePropertyValue(field, state.read(field));
            }
            auto& storage = nativeTypeStorage(scene.registry(), record.storage);
            if (storage.contains(entity))
            {
                throw std::invalid_argument("Entity already has extension component " + record.name);
            }
            storage.emplace(entity, std::move(state));
            scene.markChanged(SceneChange::eMetadata);
        };
        record.component.remove = [&record](SceneTree& scene, entt::entity entity)
        {
            nativeTypeStorage(scene.registry(), record.storage).remove(entity);
            scene.markChanged(SceneChange::eMetadata);
        };
        record.component.serialize = [&record](const entt::registry& registry, entt::entity entity)
        {
            return nativeTypeState(registry, entity, record.storage)->serialize();
        };
        record.component.take = [&record](entt::registry& registry, entt::entity entity) -> SceneComponentRestore
        {
            auto& storage = nativeTypeStorage(registry, record.storage);
            auto  state   = std::move(storage.get(entity));
            storage.remove(entity);
            return [&record, state = std::move(state)](entt::registry& target, entt::entity handle) mutable
            {
                auto& destination = nativeTypeStorage(target, record.storage);
                if (destination.contains(handle))
                {
                    throw std::invalid_argument("Duplicate detached extension component " + record.name);
                }
                destination.emplace(handle, std::move(state));
            };
        };
    }
} // namespace vultra
