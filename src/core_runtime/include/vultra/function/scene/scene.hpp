#pragma once

namespace vultra
{
    class Scene
    {
    public:
        Scene()                 = default;
        Scene(const Scene&)     = delete;
        Scene(Scene&&) noexcept = default;

        Scene& operator=(const Scene&)     = delete;
        Scene& operator=(Scene&&) noexcept = default;
    };
} // namespace vultra