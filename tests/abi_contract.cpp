#include <vultra/api/scene_bridge.hpp>
#ifdef VULTRA_TEST_GUI
#include <vultra/api/ui_bridge.hpp>
#endif
#include <vultra/scripting/native_script.hpp>

#include <iostream>
#include <stdexcept>

namespace
{
    void require(bool value, const char* message)
    {
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }
} // namespace

int main()
try
{
    using namespace vultra;
#ifdef VULTRA_TEST_GUI
    auto ui = uiApi();
#endif
    auto                        scene = sceneApi();
    VultraScriptRegistrationApi registration {};
    registration.version        = VULTRA_ABI_VERSION;
    registration.struct_size    = sizeof(registration);
    registration.register_class = [](void*, const char*, uint64_t)
    {
        return VULTRA_STATUS_OK;
    };
    VultraHostApi host {};
    host.version     = VULTRA_ABI_VERSION;
    host.struct_size = sizeof(host);
#ifdef VULTRA_TEST_GUI
    host.ui = &ui;
#endif
    host.scene   = &scene;
    host.scripts = &registration;
    VultraPluginApi plugin {};
    plugin.version     = VULTRA_ABI_VERSION;
    plugin.struct_size = sizeof(plugin);
    require(scripting::initializeNativeModule(&host, &plugin, {}) == VULTRA_STATUS_OK,
            "Matching Native SDK tables were rejected");
#ifdef VULTRA_TEST_GUI
    ui.fingerprint ^= 1;
    require(scripting::initializeNativeModule(&host, &plugin, {}) == VULTRA_STATUS_INVALID_ARGUMENT,
            "Native SDK accepted a same-sized UI table with a different contract");
    ui = uiApi();
#endif
    scene.fingerprint ^= 1;
    require(scripting::initializeNativeModule(&host, &plugin, {}) == VULTRA_STATUS_INVALID_ARGUMENT,
            "Native SDK accepted a same-sized scene table with a different contract");
    scene = sceneApi();
    require(scripting::initializeNativeModule(&host, &plugin, {}) == VULTRA_STATUS_OK,
            "Rejected ABI negotiation damaged later valid initialization");
    std::cout << "Generated ABI fingerprints and Native SDK rejection/recovery passed\n";
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
