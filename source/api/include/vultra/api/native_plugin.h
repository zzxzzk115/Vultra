#pragma once

#include <vultra/api/vultra_scene.generated.h>
#include <vultra/api/vultra_script_property.generated.h>
#include <vultra/api/vultra_ui.generated.h>

#ifdef _WIN32
#define VULTRA_PLUGIN_EXPORT __declspec(dllexport)
#else
#define VULTRA_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C"
{
#endif

    /* Tables belong to the host context; plugins borrow them until their stop callback returns. */
    typedef struct VultraExtensionApi
    {
        const char* name;
        uint64_t    name_size;
        uint32_t    version;
        uint32_t    struct_size;
        const void* api;
    } VultraExtensionApi;

    typedef struct VultraScriptRegistrationApi
    {
        uint32_t version;
        uint32_t struct_size;
        void*    user_data;
        VultraStatus (*register_class)(void* user_data, const char* name, uint64_t name_size);
    } VultraScriptRegistrationApi;

    typedef struct VultraHostApi
    {
        uint32_t              version;
        uint32_t              struct_size;
        const VultraUiApi*    ui; /* Null when the host omits api_ui. */
        const VultraSceneApi* scene;
        /* Available only while initializing a project extension. */
        const VultraScriptRegistrationApi* scripts;
        const VultraExtensionApi*          extensions;
        uint64_t                           extension_count;
    } VultraHostApi;

    typedef enum VultraSceneEvent
    {
        VULTRA_SCENE_ENTER_TREE      = 0,
        VULTRA_SCENE_READY           = 1,
        VULTRA_SCENE_PHYSICS_PROCESS = 2,
        VULTRA_SCENE_EXIT_TREE       = 3,
        /* Code replacement activates new wrappers without re-entering the native node. */
        VULTRA_SCENE_RELOADED        = 4,
        VULTRA_SCENE_PROCESS         = 5,
        VULTRA_SCENE_INPUT           = 6,
        VULTRA_SCENE_UNHANDLED_INPUT = 7
    } VultraSceneEvent;

    typedef struct VultraPluginApi
    {
        uint32_t version;
        uint32_t struct_size;
        void*    user_data;
        VultraStatus (*update)(void* user_data, VultraSceneFrame scene, float delta_seconds);
        VultraStatus (*on_gui)(void* user_data, VultraUiFrame frame); /* Optional; called only in a GUI frame. */
        VultraStatus (*stop)(void* user_data);
        VultraStatus (*on_scene)(void* user_data, VultraSceneFrame scene, uint32_t event, float delta_seconds);
        const VultraScriptPropertyApi* properties; /* Instance fields; null for extensions without instances. */
    } VultraPluginApi;

    /* The only symbol the host looks up in a native plugin. */
    typedef VultraStatus (*VultraPluginInit)(const VultraHostApi* host, VultraPluginApi* plugin);

#ifdef __cplusplus
}
#endif
