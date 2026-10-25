#include <vultra/api/native_plugin.h>

#include <stdlib.h>

typedef struct ScriptState
{
    const VultraSceneApi* scene;
} ScriptState;

static VultraStatus update(void* state, VultraSceneFrame frame, float delta)
{
    const VultraSceneApi*  scene = ((ScriptState*)state)->scene;
    uint64_t               root  = 0;
    VultraSceneTranslation position;
    if (scene->root_id(frame, &root) != VULTRA_STATUS_OK ||
        scene->node_translation(frame, root, &position) != VULTRA_STATUS_OK)
    {
        return VULTRA_STATUS_ERROR;
    }
    position.x += delta;
    return scene->set_node_translation(frame, root, position);
}

static VultraStatus stop(void* state)
{
    free(state);
    return VULTRA_STATUS_OK;
}

#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
// NOLINTNEXTLINE(readability-identifier-naming): The native loader requires this exact C symbol.
VultraStatus vultra_plugin_init(const VultraHostApi* host, VultraPluginApi* plugin)
{
    if (!host || !plugin || host->version != VULTRA_ABI_VERSION || host->struct_size != sizeof(VultraHostApi) ||
        !host->scene || host->scene->version != VULTRA_ABI_VERSION ||
        host->scene->struct_size != sizeof(VultraSceneApi))
    {
        return VULTRA_STATUS_INVALID_ARGUMENT;
    }
    ScriptState* state = malloc(sizeof(ScriptState));
    if (!state)
    {
        return VULTRA_STATUS_ERROR;
    }
    state->scene        = host->scene;
    plugin->version     = VULTRA_ABI_VERSION;
    plugin->struct_size = sizeof(VultraPluginApi);
    plugin->user_data   = state;
    plugin->update      = update;
    plugin->stop        = stop;
    return VULTRA_STATUS_OK;
}
