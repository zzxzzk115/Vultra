#include <vultra/scene/render_components.hpp>
#include <vultra/scene/scene_gpu_sync.hpp>
#include <vultra/servers/rendering/scene.hpp>

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <stdexcept>

namespace vultra
{
    void syncSceneMaterials(const SceneTree& tree, std::span<const SceneMeshInstance> instances, GpuScene& gpu)
    {
        tree.validateMaterials();
        for (const auto& instance : instances)
        {
            const auto  handle    = tree.entity(instance.node);
            const auto* component = tree.registry().try_get<SceneMesh>(handle);
            if (!component || component->model != instance.model)
            {
                throw std::invalid_argument("Material synchronization requires matching mesh topology");
            }
            const auto& mesh = *component;
            const auto& node = tree.registry().get<SceneNode>(handle);
            if (uint64_t(instance.firstMaterial) + instance.importedMaterials.size() > gpu.materials.size())
            {
                throw std::logic_error("Imported material range exceeds the GPU scene");
            }
            for (const auto& entry : mesh.materials)
            {
                if (entry.slot >= instance.importedMaterials.size())
                {
                    throw std::invalid_argument(node.name + ": material slot exceeds the imported model");
                }
            }
        }
        // Validate all references/ranges before changing the active material array.
        for (const auto& instance : instances)
        {
            const auto& mesh = tree.registry().get<SceneMesh>(tree.entity(instance.node));
            for (size_t slot = 0; slot < instance.importedMaterials.size(); ++slot)
            {
                const auto  found      = std::ranges::find(mesh.materials, uint32_t(slot), &MeshMaterialOverride::slot);
                const auto* resource   = found == mesh.materials.end() ? nullptr : tree.findMaterial(found->material);
                const auto& parameters = !resource || resource->kind() == MaterialResource::Kind::eShader ?
                                             instance.importedMaterials[slot] :
                                             resource->parameters();
                auto&       material   = gpu.materials[instance.firstMaterial + slot];
                if (MaterialParameters::fromMaterial(material) != parameters)
                {
                    parameters.applyTo(material);
                }
            }
        }
    }

    bool SceneGpuSync::needsImport(const SceneTree& tree, std::span<const SceneMeshInstance> instances) const
    {
        return (m_Root != tree.root().id() || m_Applied.structure != tree.changes().structure) &&
               !sceneMeshTopologyMatches(tree, instances);
    }

    bool SceneGpuSync::environmentChanged(const SceneTree& tree) const
    {
        return m_Root != tree.root().id() || m_Applied.structure != tree.changes().structure ||
               m_Applied.environment != tree.changes().environment;
    }

    bool SceneGpuSync::update(const SceneTree& tree, std::span<SceneMeshInstance> instances, GpuScene& gpu)
    {
        const auto& changes   = tree.changes();
        const bool  structure = m_Root != tree.root().id() || m_Applied.structure != changes.structure;
        if (structure && !sceneMeshTopologyMatches(tree, instances))
        {
            throw std::invalid_argument("Scene GPU synchronization requires reimporting changed mesh topology");
        }
        const bool materials  = structure || m_Applied.materials != changes.materials;
        const bool transforms = structure || m_Applied.transforms != changes.transforms;
        if (materials)
        {
            syncSceneMaterials(tree, instances, gpu);
        }
        if (transforms)
        {
            tree.syncTransforms();
            for (auto& instance : instances)
            {
                const auto  handle  = tree.entity(instance.node);
                const auto* spatial = tree.registry().try_get<SceneTransform>(handle);
                if (!spatial)
                {
                    throw std::logic_error("Imported mesh transform is missing after topology check");
                }
                const auto& transform = spatial->global;
                if (transform != instance.lastTransform)
                {
                    // Imported vertices already contain the initial world transform.
                    gpu.setPrimitiveTransforms(instance.firstPrimitive,
                                               instance.primitiveCount,
                                               transform * glm::inverse(instance.bakedTransform));
                    instance.lastTransform = transform;
                }
            }
        }
        m_Root    = tree.root().id();
        m_Applied = changes;
        return materials || transforms;
    }
} // namespace vultra
