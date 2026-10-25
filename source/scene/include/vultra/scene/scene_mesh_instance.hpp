#pragma once

#include <vultra/assets/asset_id.hpp>
#include <vultra/assets/material_parameters.hpp>
#include <vultra/scene/scene_components.hpp>

#include <span>
#include <vector>

namespace vultra
{
    class SceneTree;

    struct SceneMeshInstance
    {
        NodeId                          node;
        AssetId                         model;
        glm::mat4                       bakedTransform;
        glm::mat4                       lastTransform;
        uint32_t                        firstPrimitive;
        uint32_t                        primitiveCount;
        uint32_t                        firstMaterial;
        std::vector<MaterialParameters> importedMaterials;
    };

    bool sceneMeshTopologyMatches(const SceneTree& tree, std::span<const SceneMeshInstance> instances);
} // namespace vultra
