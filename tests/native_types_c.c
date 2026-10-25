#include <vultra/api/native_plugin.h>
#include <vultra/api/vultra_native_type.generated.h>

#include <stdlib.h>
#include <string.h>

typedef struct ExtensionOwner
{
    size_t instances;
} ExtensionOwner;

typedef struct ResourceInstance
{
    ExtensionOwner* owner;
} ResourceInstance;

static VultraStatus createResource(void* factory, const char* json, uint64_t size, void** result)
{
    if (!factory || !json || size != 2 || json[0] != '{' || json[1] != '}')
    {
        return VULTRA_STATUS_INVALID_ARGUMENT;
    }
    ResourceInstance* instance = malloc(sizeof(ResourceInstance));
    if (!instance)
    {
        return VULTRA_STATUS_ERROR;
    }
    instance->owner = factory;
    ++instance->owner->instances;
    *result = instance;
    return VULTRA_STATUS_OK;
}

static VultraStatus destroyResource(void* state)
{
    ResourceInstance* instance = state;
    --instance->owner->instances;
    free(instance);
    return VULTRA_STATUS_OK;
}

static VultraStatus update(void* state, VultraSceneFrame scene, float delta)
{
    (void)state;
    (void)scene;
    (void)delta;
    return VULTRA_STATUS_OK;
}

static VultraStatus stop(void* state)
{
    ExtensionOwner* owner = state;
    if (owner->instances != 0)
    {
        return VULTRA_STATUS_ERROR;
    }
    free(owner);
    return VULTRA_STATUS_OK;
}

// NOLINTNEXTLINE(readability-identifier-naming): The loader requires this C symbol.
VULTRA_PLUGIN_EXPORT VultraStatus vultra_plugin_init(const VultraHostApi* host, VultraPluginApi* plugin)
{
    if (!host || !plugin || host->version != VULTRA_ABI_VERSION || host->struct_size != sizeof(VultraHostApi))
    {
        return VULTRA_STATUS_INVALID_ARGUMENT;
    }
    const VultraTypeRegistrationApi* types = NULL;
    for (uint64_t index = 0; index < host->extension_count; ++index)
    {
        const VultraExtensionApi* domain = &host->extensions[index];
        if (domain->name_size == strlen("vultra.types") &&
            memcmp(domain->name, "vultra.types", domain->name_size) == 0 && domain->version == VULTRA_ABI_VERSION &&
            domain->struct_size == sizeof(VultraTypeRegistrationApi))
        {
            types = domain->api;
        }
    }
    if (!types)
    {
        return VULTRA_STATUS_INVALID_ARGUMENT;
    }
    ExtensionOwner* owner = calloc(1, sizeof(ExtensionOwner));
    if (!owner)
    {
        return VULTRA_STATUS_ERROR;
    }
    const VultraNativeTypeApi api =
        {VULTRA_ABI_VERSION, sizeof(VultraNativeTypeApi), createResource, destroyResource, NULL, NULL, NULL};
    const char document[] =
        "{\"version\":1,\"category\":\"resource\",\"name\":\"CResource\",\"base\":\"Resource\",\"properties\":[]}";
    if (types->register_type(types->context, document, sizeof(document) - 1, &api, owner) != VULTRA_STATUS_OK)
    {
        free(owner);
        return VULTRA_STATUS_ERROR;
    }
    plugin->version     = VULTRA_ABI_VERSION;
    plugin->struct_size = sizeof(VultraPluginApi);
    plugin->user_data   = owner;
    plugin->update      = update;
    plugin->stop        = stop;
    return VULTRA_STATUS_OK;
}
