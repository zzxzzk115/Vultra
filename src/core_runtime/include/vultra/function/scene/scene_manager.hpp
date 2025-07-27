#pragma once

#include "vultra/function/scene/scene_loader.hpp"
#include "vultra/function/scene/scene_resource_handle.hpp"

#include <entt/resource/cache.hpp>

namespace vultra
{
    using SceneCache = entt::resource_cache<SceneResource, SceneLoader>;

    class SceneManager final : public SceneCache
    {
    public:
        SceneManager();
        ~SceneManager() = default;

        [[nodiscard]] SceneResourceHandle load(const std::filesystem::path&);
    };
} // namespace vultra