#pragma once

#include <vultra/core/base/uuid.hpp>

namespace vultra
{
    namespace editor
    {
        enum class SelectType
        {
            eEntity,
            eAsset,
        };

        struct SelectEvent
        {
            CoreUUID   uuid;
            SelectType type;
        };
    } // namespace editor
} // namespace vultra