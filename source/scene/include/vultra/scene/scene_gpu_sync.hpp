#pragma once

#include <vultra/scene/scene_mesh_instance.hpp>
#include <vultra/scene/scene_tree.hpp>

namespace vultra
{
    class GpuScene;

    // Previous GPU use must be complete. Retains geometry, textures, descriptors and the compiled graph.
    void syncSceneMaterials(const SceneTree& tree, std::span<const SceneMeshInstance> instances, GpuScene& gpu);

    // One consumer's revision cursor for a GPU scene and its import ranges. Reconstruct after replacing the upload.
    // No scene changes are consumed; independent views and scripts observe the same revisions.
    class SceneGpuSync
    {
    public:
        bool needsImport(const SceneTree& tree, std::span<const SceneMeshInstance> instances) const;
        bool environmentChanged(const SceneTree& tree) const;
        // Previous GPU use must be complete. Failed synchronization leaves the cursor dirty for retry.
        bool update(const SceneTree& tree, std::span<SceneMeshInstance> instances, GpuScene& gpu);

    private:
        ObjectId     m_Root {};
        SceneChanges m_Applied;
    };
} // namespace vultra
