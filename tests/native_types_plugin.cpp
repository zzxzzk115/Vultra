#include <vultra/api/native_plugin.h>
#include <vultra/api/vultra_native_type.generated.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using Json = nlohmann::json;

    struct Host;

    struct Factory
    {
        Host* host;
        bool  moves;
    };

    struct Host
    {
        const VultraSceneApi*            scene;
        const VultraTypeRegistrationApi* registration;
        std::vector<Factory>             factories;
        size_t                           instances = 0;
    };

    struct Instance
    {
        Factory* factory;
        Json     values;
    };

    template<class Value>
    auto& fieldValue(Value& values, const std::string& name)
    {
        if (values.contains("color"))
        {
            if (name == "red")
            {
                return values.at("color").at(0);
            }
            if (name == "green")
            {
                return values.at("color").at(1);
            }
        }
        return values.at(name);
    }

    VultraStatus create(void* factory, const char* data, uint64_t size, void** result)
    {
        *result = nullptr;
        try
        {
            auto instance = std::make_unique<Instance>(static_cast<Factory*>(factory), Json::parse(data, data + size));
            if (instance->values.contains("speed") && instance->values.at("speed").get<float>() < 0)
            {
                return VULTRA_STATUS_INVALID_ARGUMENT;
            }
            ++instance->factory->host->instances;
            *result = instance.release();
            return VULTRA_STATUS_OK;
        }
        catch (...)
        {
            return VULTRA_STATUS_INVALID_ARGUMENT;
        }
    }

    VultraStatus destroy(void* state)
    {
        auto* instance = static_cast<Instance*>(state);
        --instance->factory->host->instances;
        delete instance;
        return VULTRA_STATUS_OK;
    }

    VultraStatus read(void* state, const char* name, uint64_t nameSize, char* data, uint64_t capacity, uint64_t* size)
    {
        try
        {
            const auto& instance = *static_cast<const Instance*>(state);
            const auto  json     = fieldValue(instance.values, std::string(name, nameSize)).dump();
            *size                = json.size();
            if (data && capacity < json.size())
            {
                return VULTRA_STATUS_BUFFER_TOO_SMALL;
            }
            if (data)
            {
                std::copy(json.begin(), json.end(), data);
            }
            return VULTRA_STATUS_OK;
        }
        catch (...)
        {
            return VULTRA_STATUS_INVALID_ARGUMENT;
        }
    }

    VultraStatus write(void* state, const char* name, uint64_t nameSize, const char* data, uint64_t size)
    {
        try
        {
            auto&             instance = *static_cast<Instance*>(state);
            const std::string key(name, nameSize);
            auto              candidate = instance.values;
            fieldValue(candidate, key)  = Json::parse(data, data + size);
            if ((key == "speed" || key == "value") && candidate.at(key).get<float>() < 0)
            {
                return VULTRA_STATUS_INVALID_ARGUMENT;
            }
            instance.values = std::move(candidate);
            return VULTRA_STATUS_OK;
        }
        catch (...)
        {
            return VULTRA_STATUS_INVALID_ARGUMENT;
        }
    }

    VultraStatus onScene(void* state, VultraSceneFrame frame, uint64_t object, uint32_t event, double delta)
    {
        auto& instance = *static_cast<Instance*>(state);
        auto& host     = *instance.factory->host;
        const VultraNativeTypeApi
            valid {VULTRA_ABI_VERSION, sizeof(VultraNativeTypeApi), &create, &destroy, &read, &write, &onScene};
        if (host.registration->register_type(host.registration->context, "{}", 2, &valid, nullptr) !=
            VULTRA_STATUS_INVALID_FRAME)
        {
            return VULTRA_STATUS_ERROR;
        }
        if (event != VULTRA_SCENE_PROCESS || !instance.factory->moves)
        {
            return VULTRA_STATUS_OK;
        }
        try
        {
            VultraSceneTranslation position {};
            if (host.scene->node_translation(frame, object, &position) != VULTRA_STATUS_OK)
            {
                return VULTRA_STATUS_ERROR;
            }
            const auto multiplier = instance.values.value("multiplier", 1.0f);
            position.x += instance.values.at("speed").get<float>() * multiplier * float(delta);
            instance.values.at("ticks") = instance.values.at("ticks").get<int64_t>() + 1;
            return host.scene->set_node_translation(frame, object, position);
        }
        catch (...)
        {
            return VULTRA_STATUS_ERROR;
        }
    }

    Json field(const char* name, const char* kind, Json value)
    {
        return {{"name", name},
                {"label", name},
                {"json_path", "/" + std::string(name)},
                {"kind", kind},
                {"default", std::move(value)},
                {"flags", {"serialize", "inspect", "bind"}},
                {"drawer", nullptr},
                {"widget", "drag"},
                {"speed", 0.1},
                {"integer_bits", 0},
                {"choices", Json::array()},
                {"enum_values", Json::array()}};
    }

    VultraStatus
    registerType(Host& host, const char* name, const char* category, const char* base, Json fields, bool moves = false)
    {
        const Json document {{"version", 1},
                             {"name", name},
                             {"category", category},
                             {"base", base ? Json(base) : Json(nullptr)},
                             {"properties", std::move(fields)}};
        const auto text = document.dump();
        host.factories.push_back({&host, moves});
        const VultraNativeTypeApi api {VULTRA_ABI_VERSION,
                                       sizeof(VultraNativeTypeApi),
                                       &create,
                                       &destroy,
                                       &read,
                                       &write,
                                       moves ? &onScene : nullptr};
        return host.registration->register_type(host.registration->context,
                                                text.data(),
                                                text.size(),
                                                &api,
                                                &host.factories.back());
    }

    VultraStatus update(void*, VultraSceneFrame, float)
    {
        return VULTRA_STATUS_OK;
    }

    VultraStatus stop(void* state)
    {
        auto* host = static_cast<Host*>(state);
        if (host->instances != 0)
        {
            return VULTRA_STATUS_ERROR;
        }
        delete host;
        return VULTRA_STATUS_OK;
    }
} // namespace

// NOLINTNEXTLINE(readability-identifier-naming): The loader requires this C symbol.
extern "C" VULTRA_PLUGIN_EXPORT VultraStatus vultra_plugin_init(const VultraHostApi* api, VultraPluginApi* plugin)
try
{
    if (!api || !plugin || api->version != VULTRA_ABI_VERSION || api->struct_size != sizeof(VultraHostApi))
    {
        return VULTRA_STATUS_INVALID_ARGUMENT;
    }
    const VultraTypeRegistrationApi* registration = nullptr;
    for (uint64_t index = 0; index < api->extension_count; ++index)
    {
        const auto& domain = api->extensions[index];
        if (std::string_view(domain.name, domain.name_size) == "vultra.types" && domain.version == VULTRA_ABI_VERSION &&
            domain.struct_size == sizeof(VultraTypeRegistrationApi))
        {
            registration = static_cast<const VultraTypeRegistrationApi*>(domain.api);
        }
    }
    if (!registration)
    {
        return VULTRA_STATUS_INVALID_ARGUMENT;
    }
    auto host          = std::make_unique<Host>();
    host->scene        = api->scene;
    host->registration = registration;
    host->factories.reserve(10);
    auto ticks            = field("ticks", "int", int64_t {0});
    ticks["integer_bits"] = 32;
#ifdef TEST_NATIVE_TYPES_INVALID
    if (registerType(*host, "TestMover", "node", "Node3D", {field("speed", "float", 1.0f), ticks}, true) !=
        VULTRA_STATUS_OK)
    {
        return VULTRA_STATUS_ERROR;
    }
    const VultraNativeTypeApi invalid {2, sizeof(VultraNativeTypeApi), &create, &destroy, &read, &write, nullptr};
    const auto                invalidStatus =
        host->registration->register_type(host->registration->context, "{}", 2, &invalid, nullptr);
    return invalidStatus == VULTRA_STATUS_INVALID_ARGUMENT ? VULTRA_STATUS_ERROR : VULTRA_STATUS_OK;
#elif defined(TEST_NATIVE_TYPES_DERIVED)
    if (registerType(*host, "OtherMover", "node", "TestMover", {field("boost", "float", 1.0f)}, true) !=
        VULTRA_STATUS_OK)
    {
        return VULTRA_STATUS_ERROR;
    }
#else
    auto target           = field("target", "reference", nullptr);
    target["reference"]   = {{"category", "node"}, {"type", "Node3D"}, {"nullable", true}};
    auto settings         = field("settings", "reference", nullptr);
    settings["reference"] = {{"category", "resource"}, {"type", "TestSettings"}, {"nullable", true}};
    auto red              = field("red", "float", 0.1f);
    red["json_path"]      = "/color/0";
    auto green            = field("green", "float", 0.2f);
    green["json_path"]    = "/color/1";
    if (registerType(*host, "TestMover", "node", "Node3D", {field("speed", "float", 1.0f), ticks}, true) !=
            VULTRA_STATUS_OK ||
        registerType(*host, "FastMover", "node", "TestMover", {field("multiplier", "float", 2.0f)}, true) !=
            VULTRA_STATUS_OK ||
        registerType(*host, "TestSettings", "resource", "Resource", {field("drag", "float", 1.0f), target}) !=
            VULTRA_STATUS_OK ||
        registerType(*host, "TestVelocity", "component", nullptr, {field("value", "float", 1.0f), settings}) !=
            VULTRA_STATUS_OK ||
        registerType(*host, "TestCamera", "node", "Camera3D", {field("aperture", "float", 2.0f)}) != VULTRA_STATUS_OK ||
        registerType(*host, "TestLight", "node", "SpotLight3D", Json::array()) != VULTRA_STATUS_OK ||
        registerType(*host, "TestEnvironment", "node", "WorldEnvironment", Json::array()) != VULTRA_STATUS_OK ||
        registerType(*host, "TestMesh", "node", "MeshInstance3D", {field("lod", "float", 0.0f)}) != VULTRA_STATUS_OK ||
        registerType(*host, "GroupedResource", "resource", "Resource", {red, green}) != VULTRA_STATUS_OK ||
        registerType(*host, "TestMaterial", "resource", "MaterialResource", {field("absorption", "float", 0.5f)}) !=
            VULTRA_STATUS_OK)
    {
        return VULTRA_STATUS_ERROR;
    }
#endif
    plugin->version     = VULTRA_ABI_VERSION;
    plugin->struct_size = sizeof(VultraPluginApi);
    plugin->user_data   = host.release();
    plugin->update      = &update;
    plugin->stop        = &stop;
    return VULTRA_STATUS_OK;
}
catch (...)
{
    return VULTRA_STATUS_ERROR;
}
