#include <vultra/scripting/native_script.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>

namespace
{
    class ExportProbe final : public vultra::scripting::NativeScript
    {
    public:
#ifdef TEST_EXPORT_RELOAD
        float speed = 4;
#else
        float speed = 1;
#endif
        float offset     = 0;
        float readySpeed = 0;
#ifdef TEST_GROUP_NARROW
        int8_t steps = 0;
#else
        int32_t steps = 0;
#endif
        bool        enabled = true;
        std::string caption = "ready";

        void onReady() override
        {
            const auto fields = nlohmann::json::parse(actor().propertyListJson());
            if (fields.at("version") != 1 || fields.at("properties").size() < 6)
            {
                throw std::runtime_error("Node omitted script property metadata");
            }
            actor().setPropertyJson("readySpeed", actor().propertyJson("speed"));
            if (caption == "broadcast")
            {
                const auto root = sceneRoot();
                for (uint64_t index = 0; index < root.childCount(); ++index)
                {
                    const auto peer = root.child(index);
                    if (peer.name() == actor().name())
                    {
                        continue;
                    }
                    const auto metadata = nlohmann::json::parse(peer.propertyListJson());
                    for (const auto& property : metadata.at("properties"))
                    {
                        const auto name = property.at("name").get<std::string>();
                        if (name == "caption" || name == "Caption")
                        {
                            peer.setPropertyJson(name, "\"Native broadcast\"");
                        }
                    }
                }
            }
        }

        void onProcess(float delta) override
        {
            if (enabled)
            {
                auto position = actor().position();
                position.x += speed * delta;
                position.y = offset;
                actor().setPosition(position);
                ++steps;
            }
        }
    };
} // namespace

#ifdef TEST_RELOAD_ACTIVATION_FAILURE
#define vultra_plugin_init initializeExportProbe
#endif
VULTRA_NATIVE_MODULE(vultra::scripting::scriptClass<ExportProbe>(
    "ExportProbe",
    {vultra::scripting::scriptProperty("speed", &ExportProbe::speed),
     vultra::scripting::scriptProperty("offset", &ExportProbe::offset),
     vultra::scripting::scriptProperty("readySpeed", &ExportProbe::readySpeed),
     vultra::scripting::scriptProperty("steps", &ExportProbe::steps),
     vultra::scripting::scriptProperty("enabled", &ExportProbe::enabled),
     vultra::scripting::scriptProperty("caption", &ExportProbe::caption)},
    "Node3D"))

#ifdef TEST_RELOAD_ACTIVATION_FAILURE
#undef vultra_plugin_init

namespace
{
    decltype(VultraPluginApi::on_scene) sceneCallback = nullptr;

    VultraStatus failActivation(void* data, VultraSceneFrame frame, uint32_t event, float delta)
    {
        if (event == VULTRA_SCENE_RELOADED)
        {
            return VULTRA_STATUS_ERROR;
        }
        return sceneCallback(data, frame, event, delta);
    }
} // namespace

// NOLINTNEXTLINE(readability-identifier-naming): The loader requires this C symbol.
extern "C" VULTRA_PLUGIN_EXPORT VultraStatus vultra_plugin_init(const VultraHostApi* host, VultraPluginApi* plugin)
{
    const auto status = initializeExportProbe(host, plugin);
    if (status == VULTRA_STATUS_OK)
    {
        sceneCallback    = plugin->on_scene;
        plugin->on_scene = &failActivation;
    }
    return status;
}
#endif
