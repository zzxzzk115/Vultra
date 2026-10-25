#pragma once

#include <vultra/api/vultra_scene.generated.h>
#include <vultra/core/base/api_annotations.hpp>

#include <cstdint>

namespace vultra
{
    // The generator owns the C/C# layouts. State is borrowed only while the script instance is alive.
    struct VULTRA_ABI_TABLE ScriptPropertyApi
    {
        uint32_t version;
        uint32_t structSize;
        VultraStatus (*describe)(void* state, char* data, uint64_t capacity, uint64_t* size);
        VultraStatus (*read)(void*            state,
                             VultraSceneFrame scene,
                             const char*      name,
                             uint64_t         nameSize,
                             char*            data,
                             uint64_t         capacity,
                             uint64_t*        size);
        VultraStatus (*write)(void*            state,
                              VultraSceneFrame scene,
                              const char*      name,
                              uint64_t         nameSize,
                              const char*      json,
                              uint64_t         jsonSize);
        // Optional deferred reference restoration, after all group instances have been constructed.
        VultraStatus (*prepare)(void* state, VultraSceneFrame scene);
    };
} // namespace vultra
