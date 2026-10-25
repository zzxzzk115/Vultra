#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/scene/node_3d.hpp>
#include <vultra/scene/scene_tree.hpp>
#include <vultra/scripting/script_host.hpp>
#include <vultra/ui/editor_gui.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }
} // namespace

int main()
try
{
    using namespace vultra;
    const auto directory = std::filesystem::absolute("build/.tmp") / ("lua-gui-" + StableId::generate().toString());
    std::filesystem::create_directories(directory);
    const auto path = directory / "gui_scope.lua";
    {
        std::ofstream file(path);
        file << R"lua(
package.preload.vultra_gui_scope_test = function() return {} end
local shared = require('vultra_gui_scope_test')
local Script = {
    class_name='GuiScopeProbe', extends='Node3D', exports={
        uiCalls={kind='int',integer_bits=32,default=0},
        rejectedScene={kind='bool',default=false},
        rejectedUi={kind='bool',default=false}
    }
}
function Script:_ready() self.label = self.node:name() end
function Script:_process(delta)
    if shared.draw then self.rejectedUi = not pcall(shared.draw, 'wrong phase') end
end
function Script:_editor_gui(gui)
    if not shared.draw then shared.draw = function(text) gui:text(text) end end
    self.rejectedScene = not pcall(function() return self.node:name() end)
    shared.draw(self.label .. ': world-owned Lua UI')
    self.uiCalls = self.uiCalls + 1
end
return Script
)lua";
        require(bool(file), "Write Lua GUI fixture");
    }
    Device       device;
    EditorGui    gui(device, VriFormat_RGBA8_UNORM, {.multiViewport = false, .persistLayout = false});
    const Extent extent {480, 220};
    Texture      target(device, colorTexture(extent));
    Frame        frame(device);
    SceneTree    world(std::make_unique<Node>("GUI callback world"));
    auto&        creator  = world.addChild(world.root(), std::make_unique<Node3D>("Creator"));
    auto&        survivor = world.addChild(world.root(), std::make_unique<Node3D>("Survivor"));
    ScriptHost   host(world);
    for (const auto* node : {&creator, &survivor})
    {
        host.add({ScriptLanguage::eLua, path, "GuiScopeProbe", node->idInScene().value});
    }
    for (int index = 0; index < 3; ++index)
    {
        if (index == 2)
        {
            auto removed = world.remove(creator);
            removed.reset();
        }
        host.update(1.0f / 60);
        gui.begin(extent, 1.0f / 60);
        auto ui = gui.frame();
        ui.setNextWindowPos({20, 20}, ImGuiCond_Always);
        ui.setNextWindowSize({430, 170}, ImGuiCond_Always);
        {
            EditorGuiWindow window(ui, "Lua callback scopes");
            require(bool(window), "Lua GUI test window is not visible");
            host.gui(gui);
        }
        gui.upload(extent);
        auto* command = frame.begin();
        gui.copy(command);
        target.transition(
            command,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        const float clear[4] {0, 0, 0, 1};
        beginColorPass(device, command, target.view(), extent, clear);
        gui.draw(command);
        device.core.CmdEndRendering(command);
        frame.submitAndWait();
    }
    require(host.readExport(survivor.id(), "uiCalls") == PropertyValue(int64_t {3}) &&
                host.readExport(survivor.id(), "rejectedUi") == PropertyValue(true) &&
                host.readExport(survivor.id(), "rejectedScene") == PropertyValue(true),
            "Lua UI/scene borrows crossed phases or cached GUI closures retained their creator");
    const auto image   = readback(device, target);
    bool       hasText = false;
    for (uint32_t y = 45; y < 100; ++y)
    {
        for (uint32_t x = 25; x < 400; ++x)
        {
            hasText |= image.rgba[(size_t(y) * extent.width + x) * 4] > 0.5f;
        }
    }
    require(hasText, "Lua GUI commands did not reach the window body in the offscreen image");
    savePng(image, directory / "lua-gui.png");
    std::cout << "Lua GUI scopes passed: " << (directory / "lua-gui.png").string() << '\n';
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
