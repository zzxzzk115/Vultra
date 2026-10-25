#include "scene_property_validation.hpp"

#include <vultra/scene/scene_tree.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace vultra
{
    void detail::validateSceneProperty(const PropertySchema& property)
    {
        property.validateDefinition();
        const bool reference =
            property.kind == PropertyKind::eReference ||
            (property.kind == PropertyKind::eArray && property.elementKind == PropertyKind::eReference);
        if (reference && property.referenceCategory != "node" && property.referenceCategory != "resource" &&
            property.referenceCategory != "asset")
        {
            throw std::invalid_argument("Scene reference requires a node, resource or asset category: " +
                                        property.name);
        }
        if (property.referenceClass && property.referenceCategory != "node")
        {
            throw std::invalid_argument("Behavior class constraints require node references: " + property.name);
        }
    }

    namespace
    {
        const NodePropertyBinding* propertyBinding(const SceneTree& scene, ObjectId id)
        {
            if (const auto* node = scene.find(id))
            {
                return scene.registry().get<SceneProcess>(node->entity()).properties;
            }
            return nullptr;
        }
    } // namespace

    const SceneTypeCatalog& SceneTree::types() const
    {
        return m_Types;
    }

    Object* SceneTree::findObject(ObjectId id) const
    {
        if (auto* node = find(id))
        {
            return node;
        }
        return findResource(id);
    }

    const ObjectTypeInfo& SceneTree::objectType(ObjectId id) const
    {
        const auto* object = findObject(id);
        if (!object)
        {
            throw std::invalid_argument("Property target does not belong to this world");
        }
        return m_Types.objects().type(object->typeName());
    }

    std::vector<PropertySchema> SceneTree::propertyList(ObjectId id) const
    {
        std::vector<PropertySchema> result;
        for (const auto& property : objectType(id).properties)
        {
            result.push_back(property);
        }
        if (const auto* binding = propertyBinding(*this, id))
        {
            result.insert(result.end(), binding->properties.begin(), binding->properties.end());
        }
        return result;
    }

    const PropertySchema& SceneTree::propertySchema(ObjectId id, std::string_view name) const
    {
        const auto& type   = objectType(id);
        const auto  native = std::ranges::find(type.properties, name, &PropertyInfo::name);
        if (native != type.properties.end())
        {
            return *native;
        }
        if (const auto* binding = propertyBinding(*this, id))
        {
            const auto field = std::ranges::find(binding->properties, name, &PropertySchema::name);
            if (field != binding->properties.end())
            {
                return *field;
            }
        }
        throw std::invalid_argument(std::string(type.name) + ": unknown property " + std::string(name));
    }

    std::span<const PropertyClassId> SceneTree::behaviorClasses(ObjectId id) const
    {
        objectType(id);
        const auto* binding = propertyBinding(*this, id);
        return binding ? binding->classes : std::span<const PropertyClassId> {};
    }

    PropertyValue SceneTree::readProperty(ObjectId id, std::string_view name) const
    {
        const auto& type   = objectType(id);
        const auto  native = std::ranges::find(type.properties, name, &PropertyInfo::name);
        if (native != type.properties.end())
        {
            return native->read(dynamic_cast<const void*>(findObject(id)));
        }
        propertySchema(id, name);
        const auto* binding = propertyBinding(*this, id);
        return binding->read(binding->state, name);
    }

    void SceneTree::writeProperty(ObjectId               id,
                                  std::string_view       name,
                                  const PropertyValue&   value,
                                  const ProjectManifest* project)
    {
        const auto& property = propertySchema(id, name);
        validatePropertyValue(m_Types, property, value, project, false);
        const auto& type   = objectType(id);
        const auto  native = std::ranges::find(type.properties, name, &PropertyInfo::name);
        if (native != type.properties.end())
        {
            native->write(dynamic_cast<void*>(findObject(id)), value);
            return;
        }
        const auto* binding = propertyBinding(*this, id);
        binding->write(binding->state, name, value);
    }

    const SceneComponentType& SceneTree::componentType(ObjectId id, std::string_view name) const
    {
        const auto handle = entity(id);
        if (handle == entt::null)
        {
            throw std::invalid_argument("Component target does not belong to this world");
        }
        const auto& type = m_Types.componentType(name);
        if (!type.get(*m_Registry, handle))
        {
            throw std::invalid_argument("Entity has no component: " + std::string(name));
        }
        return type;
    }

    PropertyValue SceneTree::readComponentProperty(ObjectId id, std::string_view type, std::string_view property) const
    {
        const auto& record = componentType(id, type);
        return record.type.property(property).read(record.get(*m_Registry, entity(id)));
    }

    void SceneTree::addComponent(ObjectId id, std::string_view type, std::string_view state)
    {
        requireIdle();
        const auto  handle = entity(id);
        const auto& record = m_Types.componentType(type);
        if (handle == entt::null || !record.create || record.get(*m_Registry, handle))
        {
            throw std::invalid_argument("Component construction requires a live entity, factory and absent component");
        }
        record.create(*this, handle, state);
        try
        {
            for (const auto& property : record.type.properties)
            {
                validatePropertyValue(property, property.read(record.get(*m_Registry, handle)));
            }
        }
        catch (...)
        {
            record.remove(*this, handle);
            throw;
        }
    }

    void SceneTree::removeComponent(ObjectId id, std::string_view type)
    {
        requireIdle();
        const auto& record = componentType(id, type);
        if (!record.remove)
        {
            throw std::invalid_argument("Component is owned by its native facade");
        }
        record.remove(*this, entity(id));
    }

    void SceneTree::writeComponentProperty(ObjectId               id,
                                           std::string_view       type,
                                           std::string_view       name,
                                           const PropertyValue&   value,
                                           const ProjectManifest* project)
    {
        const auto& record   = componentType(id, type);
        const auto& property = record.type.property(name);
        validatePropertyValue(m_Types, property, value, project, false);
        record.write(*this, entity(id), property, value);
    }

    void SceneTree::validatePropertyValue(const PropertySchema&            property,
                                          const PropertyValue&             value,
                                          const ProjectManifest*           project,
                                          std::span<const BehaviorBinding> proposed) const
    {
        validatePropertyValue(m_Types, property, value, project, false, proposed);
    }

    void SceneTree::validatePropertyValue(const SceneTypeCatalog&          types,
                                          const PropertySchema&            property,
                                          const PropertyValue&             value,
                                          const ProjectManifest*           project,
                                          bool                             deferExternalAssets,
                                          std::span<const BehaviorBinding> proposed) const
    {
        property.validate(value);
        auto validateReference = [&](const PropertyValue& item)
        {
            const auto reference = std::get<PropertyReference>(item);
            if (!reference.id.valid())
            {
                return;
            }
            if (property.referenceCategory == "node")
            {
                const auto* target = find(NodeId {reference.id});
                if (!target || !types.objects().type(target->typeName()).isA(property.referenceType))
                {
                    throw std::invalid_argument("Reference requires a live " + std::string(property.referenceType) +
                                                " in this world");
                }
                if (property.referenceClass)
                {
                    const auto  next = std::ranges::find_if(proposed,
                                                            [&](const auto& binding)
                                                            {
                                                               return &binding.node == target;
                                                            });
                    const auto* binding =
                        next == proposed.end() ? propertyBinding(*this, target->id()) : next->properties;
                    if (property.referenceClass->module.empty() || !binding ||
                        std::ranges::find(binding->classes, *property.referenceClass) == binding->classes.end())
                    {
                        throw std::invalid_argument("Reference requires behavior class " +
                                                    property.referenceClass->name + " from " +
                                                    property.referenceClass->module);
                    }
                }
            }
            else if (property.referenceCategory == "resource")
            {
                const auto* target = findResource(ResourceId {reference.id});
                if (!target || !types.objects().type(target->typeName()).isA(property.referenceType))
                {
                    throw std::invalid_argument("Reference requires a live scene resource of type " +
                                                property.referenceType);
                }
            }
            else if (property.referenceCategory == "asset")
            {
                const auto externalType = std::string_view(property.referenceType);
                if (externalType != "ModelResource" && externalType != "Resource")
                {
                    throw std::invalid_argument("Reference requires a loaded " + std::string(externalType));
                }
                if (!project && deferExternalAssets)
                {
                    return;
                }
                if (!project)
                {
                    throw std::invalid_argument("Asset references require the owning project catalog");
                }
                const auto& asset = project->asset(AssetId {reference.id});
                if (externalType == "ModelResource")
                {
                    const auto extension = asset.path.extension();
                    if (extension != ".gltf" && extension != ".glb" && extension != ".obj" && extension != ".fbx")
                    {
                        throw std::invalid_argument("Reference is not an importable model asset");
                    }
                }
            }
        };
        if (property.kind == PropertyKind::eReference)
        {
            validateReference(value);
        }
        else if (property.kind == PropertyKind::eArray && property.elementKind == PropertyKind::eReference)
        {
            for (const auto& item : std::get<PropertyArray>(value).values)
            {
                validateReference(item);
            }
        }
    }

    void SceneTree::validateReferences(const ProjectManifest* project) const
    {
        validateReferences(m_Types, project);
    }

    void SceneTree::validateReferences(const SceneTypeCatalog& types, const ProjectManifest* project) const
    {
        auto validate = [&](const Object& object)
        {
            const auto& type = types.objects().type(object.typeName());
            for (const auto& property : type.properties)
            {
                if (hasPropertyFlag(property.flags, PropertyFlags::eSerialize))
                {
                    validatePropertyValue(types,
                                          property,
                                          property.read(dynamic_cast<const void*>(&object)),
                                          project,
                                          true);
                }
            }
        };
        for (const auto handle : orderedEntities())
        {
            if (const auto* node = m_Registry->get<SceneNode>(handle).node)
            {
                validate(*node);
            }
        }
        for (const auto& resource : m_Resources)
        {
            validate(*resource);
        }
        for (const auto handle : orderedEntities())
        {
            for (const auto* record : m_Types.components())
            {
                if (const auto* value = record->get(*m_Registry, handle); value && record->serialize)
                {
                    for (const auto& property : record->type.properties)
                    {
                        validatePropertyValue(types, property, property.read(value), project, true);
                    }
                }
            }
        }
    }
} // namespace vultra
