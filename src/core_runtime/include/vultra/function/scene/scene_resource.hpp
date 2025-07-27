#pragma once

#include "vultra/function/resource/resource.hpp"
#include "vultra/function/scene/scene.hpp"

namespace vultra
{
    class SceneResource final : public resource::Resource, public Scene
    {
    public:
        SceneResource() = default;
        explicit SceneResource(Scene&&, const std::filesystem::path&);
        SceneResource(const SceneResource&)     = delete;
        SceneResource(SceneResource&&) noexcept = default;

        SceneResource& operator=(const SceneResource&)     = delete;
        SceneResource& operator=(SceneResource&&) noexcept = default;
    };
} // namespace vultra