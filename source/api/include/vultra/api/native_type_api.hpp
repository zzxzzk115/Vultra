#pragma once

#include <vultra/api/vultra_scene.generated.h>
#include <vultra/core/base/api_annotations.hpp>

namespace vultra
{
    // Source of the generated C layouts. Instance state belongs to the module; the host owns its lifetime.
    struct VULTRA_ABI_TABLE NativeTypeApi
    {
        uint32_t version;
        uint32_t structSize;
        VultraStatus (*create)(void* factory, const char* json, uint64_t jsonSize, void** instance);
        VultraStatus (*destroy)(void* instance);
        VultraStatus (
            *read)(void* instance, const char* name, uint64_t nameSize, char* data, uint64_t capacity, uint64_t* size);
        VultraStatus (*write)(void* instance, const char* name, uint64_t nameSize, const char* json, uint64_t jsonSize);
        VultraStatus (
            *onScene)(void* instance, VultraSceneFrame scene, uint64_t object, uint32_t event, double deltaSeconds);
    };

    // Available only during extension initialization; descriptions and callback tables are copied by the host.
    struct VULTRA_ABI_TABLE TypeRegistrationApi
    {
        uint32_t version;
        uint32_t structSize;
        void*    context;
        VultraStatus (
            *registerType)(void* context, const char* json, uint64_t jsonSize, const NativeTypeApi* api, void* factory);
    };
} // namespace vultra
