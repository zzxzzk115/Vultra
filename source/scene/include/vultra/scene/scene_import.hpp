#pragma once

#include <vultra/assets/asset_pipeline.hpp>
#include <vultra/assets/project_manifest.hpp>
#include <vultra/scene/scene_mesh_instance.hpp>
#include <vultra/scene/scene_tree.hpp>

#include <vector>

namespace vultra
{
    // Bake mesh nodes into one uploadable scene. Each distinct model is imported once.
    // Optional instance ranges let the runtime apply later node transforms without reuploading geometry.
    ImportedAsset importScene(const SceneTree&                tree,
                              const ProjectManifest&          project,
                              const std::filesystem::path&    projectRoot,
                              const AssetImportOptions&       options   = {},
                              std::vector<SceneMeshInstance>* instances = nullptr,
                              const AssetSource*              source    = nullptr);
} // namespace vultra
