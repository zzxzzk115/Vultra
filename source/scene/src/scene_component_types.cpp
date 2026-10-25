#include "scene_property_validation.hpp"

#include <vultra/scene/component_properties.generated.hpp>
#include <vultra/scene/scene_tree.hpp>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace vultra
{
    namespace
    {
        template<class Component>
        const void* componentData(const entt::registry& registry, entt::entity entity)
        {
            return registry.try_get<Component>(entity);
        }

        template<class Component>
        void
        writeComponent(SceneTree& scene, entt::entity entity, const PropertyInfo& property, const PropertyValue& value)
        {
            auto&       registry = scene.registry();
            const auto& current  = registry.get<Component>(entity);
            // Mesh material arrays stay in place when editing the sole published field, model.
            auto candidate = [&]
            {
                if constexpr (std::is_same_v<Component, SceneMesh>)
                {
                    return SceneMesh {current.model, {}};
                }
                else
                {
                    return current;
                }
            }();
            property.write(&candidate, value);
            if constexpr (std::is_same_v<Component, SceneNode>)
            {
                if (candidate.node && candidate.name.empty())
                {
                    throw std::invalid_argument("Scene node name is empty");
                }
            }
            else if constexpr (std::is_same_v<Component, SceneTransform>)
            {
                SceneTransform::validate(candidate.local);
            }
            else if constexpr (std::is_same_v<Component, SceneProcess>)
            {
                if (candidate.mode < ProcessMode::eInherit || candidate.mode > ProcessMode::eDisabled)
                {
                    throw std::invalid_argument("Unknown process mode");
                }
            }
            else if constexpr (std::is_same_v<Component, SceneMesh>)
            {
                if (!candidate.model.value.valid())
                {
                    throw std::invalid_argument("Mesh requires a model asset ID");
                }
            }
            else if constexpr (std::is_same_v<Component, SceneCamera>)
            {
                SceneCamera::validate(candidate.settings);
            }
            else if constexpr (std::is_same_v<Component, SceneLight>)
            {
                if (candidate.kind < RenderLightKind::eDirectional || candidate.kind > RenderLightKind::eSpot)
                {
                    throw std::invalid_argument("Unknown light kind");
                }
                SceneLight::validate(candidate.settings);
            }
            else if constexpr (std::is_same_v<Component, SceneEnvironment>)
            {
                SceneEnvironment::validate(candidate.settings);
            }
            if (property.read(&current) == property.read(&candidate))
            {
                return;
            }
            registry.patch<Component>(entity,
                                      [&](Component& component)
                                      {
                                          if constexpr (std::is_same_v<Component, SceneMesh>)
                                          {
                                              component.model = candidate.model;
                                          }
                                          else
                                          {
                                              component = std::move(candidate);
                                          }
                                      });
        }

        const std::array kComponents {
            SceneComponentType {sceneNodeType(), &componentData<SceneNode>, &writeComponent<SceneNode>},
            SceneComponentType {sceneHierarchyType(), &componentData<SceneHierarchy>, nullptr},
            SceneComponentType {sceneProcessType(), &componentData<SceneProcess>, &writeComponent<SceneProcess>},
            SceneComponentType {sceneTransformType(), &componentData<SceneTransform>, &writeComponent<SceneTransform>},
            SceneComponentType {sceneMeshType(), &componentData<SceneMesh>, &writeComponent<SceneMesh>},
            SceneComponentType {sceneCameraType(), &componentData<SceneCamera>, &writeComponent<SceneCamera>},
            SceneComponentType {sceneLightType(), &componentData<SceneLight>, &writeComponent<SceneLight>},
            SceneComponentType {sceneEnvironmentType(),
                                &componentData<SceneEnvironment>,
                                &writeComponent<SceneEnvironment>}};
    } // namespace

    void SceneTypeCatalog::addBuiltinComponents()
    {
        for (const auto& record : kComponents)
        {
            add(record);
        }
    }

    void SceneTypeCatalog::add(const SceneComponentType& record)
    {
        if (record.type.category != "component" || !record.get || (!record.type.properties.empty() && !record.write))
        {
            throw std::invalid_argument("Component type requires storage access and a writer for published fields");
        }
        if ((record.create || record.remove || record.serialize || record.take) &&
            (!record.create || !record.remove || !record.serialize || !record.take))
        {
            throw std::invalid_argument("Constructible components require removal, persistence and detach transfer");
        }
        m_Components.reserve(m_Components.size() + 1);
        m_Lifetimes.reserve(m_Lifetimes.size() + 1);
        for (const auto& property : record.type.properties)
        {
            detail::validateSceneProperty(property);
        }
        m_Objects.add(record.type);
        m_Components.push_back(&record);
        borrow(record.lifetime);
    }

    void SceneTypeCatalog::remove(const SceneComponentType& record)
    {
        const auto found = std::ranges::find(m_Components, &record);
        if (found == m_Components.end())
        {
            throw std::invalid_argument("Component type does not belong to this catalog");
        }
        m_Objects.remove(record.type);
        m_Components.erase(found);
        release(record.lifetime);
    }

    const SceneComponentType& SceneTypeCatalog::componentType(std::string_view name) const
    {
        const auto found = std::ranges::find_if(m_Components,
                                                [&](const auto* record)
                                                {
                                                    return record->type.name == name;
                                                });
        if (found == m_Components.end())
        {
            throw std::invalid_argument("Unknown component type: " + std::string(name));
        }
        return **found;
    }

    std::span<const SceneComponentType* const> SceneTypeCatalog::components() const
    {
        return m_Components;
    }
} // namespace vultra
