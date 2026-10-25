#pragma once

#include <vultra/drivers/rhi/shader_program.hpp>
#include <vultra/scene/scene_types.hpp>

#include <filesystem>

namespace vultra
{
    // Export validates assets/cooks shaders using the borrowed type catalog; it never implicitly loads code.
    void packProject(const std::filesystem::path& projectFile,
                     const std::filesystem::path& output,
                     const ShaderCompileOptions&  shaders = {},
                     const SceneTypeCatalog&      types   = {});
    void packBuiltins(const std::filesystem::path& engineRoot, const std::filesystem::path& output);
} // namespace vultra
