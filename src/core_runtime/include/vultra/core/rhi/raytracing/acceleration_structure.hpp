#pragma once

#include "vultra/core/rhi/vk/vulkan_include.hpp"

namespace vultra
{
    namespace rhi
    {
        struct AccelerationStructure
        {
        public:
            [[nodiscard]] vk::AccelerationStructureKHR getHandle() const;

        private:
            vk::AccelerationStructureKHR m_Handle {nullptr};
        };
    } // namespace rhi
} // namespace vultra