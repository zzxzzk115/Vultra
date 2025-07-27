#pragma once

#include "vultra/function/scene/scene_resource.hpp"

namespace vultra
{
    struct SceneLoader final : entt::resource_loader<SceneResource>
    {
        result_type operator()(const std::filesystem::path&);
        result_type operator()(Scene&&) const;
    };
} // namespace vultra