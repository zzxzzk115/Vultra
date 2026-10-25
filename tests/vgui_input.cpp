#include <vultra/core/base/stable_id.hpp>
#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/platform/os/file.hpp>
#include <vultra/scene/scene_tree.hpp>
#include <vultra/ui/vgui.hpp>

#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>

namespace
{
    using namespace vultra;

    void require(bool value, const char* message)
    {
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }

    class InputProbe final : public Node
    {
    public:
        InputProbe() :
            Node("Input probe")
        {
        }

        size_t keyPresses    = 0;
        size_t mousePresses  = 0;
        size_t textEvents    = 0;
        float  requestedGain = 0.1f;
        float  renderedGain  = 0;

        void onInput(InputEvent& event) override
        {
            if (const auto* key = std::get_if<KeyInput>(&event.value);
                key && key->key == KeyCode::eA && key->action == InputAction::ePress)
            {
                ++keyPresses;
            }
            if (const auto* button = std::get_if<MouseButtonInput>(&event.value); button && button->held)
            {
                ++mousePresses;
            }
            textEvents += std::holds_alternative<TextInput>(event.value);
        }

        void onProcess(double) override
        {
            renderedGain = requestedGain;
        }
    };
} // namespace

int main()
try
{
    using namespace vultra;
    namespace fs   = std::filesystem;
    const auto run = fs::absolute(fs::path("build/.tmp") / ("vgui-input-" + StableId::generate().toString()));
    fs::create_directories(run);
    constexpr std::string_view document = R"(<rml><head><style>
body { font-family: LatoLatin; width: 100%; height: 100%; margin: 0; pointer-events: none; }
#panel { position: absolute; left: 10px; top: 10px; width: 280px; height: 230px;
         background-color: #242b33; pointer-events: auto; }
#field { position: absolute; left: 20px; top: 20px; width: 180px; height: 26px; }
#toggle { position: absolute; left: 20px; top: 75px; width: 22px; height: 22px; }
#apply { position: absolute; left: 20px; top: 120px; width: 180px; height: 28px; }
#scroll { position: absolute; left: 340px; top: 20px; width: 140px; height: 180px;
          overflow: auto; background-color: #324050; pointer-events: auto; }
#content { height: 600px; }
</style></head><body><div id="panel">
<input id="field" type="text"/><input id="toggle" type="checkbox"/>
<button id="apply">Apply gain</button></div><div id="scroll"><div id="content">Scroll area</div></div>
</body></rml>)";
    writeFileAtomically(run / "input.rml", std::as_bytes(std::span(document)));
    const Extent size {640, 360};
    Device       device;
    Texture      target(device, colorTexture(size));
    Frame        frame(device);
    size_t       changes = 0;
    size_t       clicks  = 0;
    SceneTree    world(std::make_unique<InputProbe>());
    auto&        probe = static_cast<InputProbe&>(world.root());
    VGui         gui(device, size, VriFormat_RGBA8_UNORM);
    gui.loadFont("resources/ui/LatoLatin-Regular.ttf");
    gui.loadDocument(run / "input.rml");
    world.input().bind("fire", KeyCode::eA);
    world.input().bind("activate", MouseCode::eLeft);
    world.input().bind("modifier", KeyCode::eLCtrl);
    gui.bindChange("toggle",
                   [&]
                   {
                       ++changes;
                   });
    gui.bindClick("apply",
                  [&]
                  {
                      ++clicks;
                      probe.requestedGain = 0.75f;
                  });
    Input      input;
    const auto step = [&](InputCapture earlier = {})
    {
        gui.processInput(input, size, earlier);
        world.input().update(input.events(), earlier);
        world.advance(0, input.events(), earlier);
        gui.update(size);
        auto* cmd = frame.begin();
        target.transition(
            cmd,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        const float clear[] {probe.renderedGain, 0.05f, 0.05f, 1};
        beginColorPass(device, cmd, target.view(), size, clear);
        device.core.CmdEndRendering(cmd);
        gui.draw(cmd, target);
        frame.submitAndWait();
    };
    const auto click = [&](glm::vec2 position)
    {
        input.setMousePosition(position);
        input.setMouseButton(MouseCode::eLeft, true);
        input.setMouseButton(MouseCode::eLeft, false);
    };
    input.advanceFrame();
    step();
    const auto baseline = readback(device, target);

    click({600, 330});
    input.setKey(KeyCode::eA, InputAction::ePress);
    input.setKey(KeyCode::eA, InputAction::eRelease);
    click({50, 45});
    input.addText("ab");
    input.setKey(KeyCode::eLCtrl, InputAction::ePress);
    input.setKey(KeyCode::eA, InputAction::ePress);
    input.setKey(KeyCode::eA, InputAction::eRelease);
    input.setKey(KeyCode::eLCtrl, InputAction::eRelease);
    input.addText("Z");
    input.setKey(KeyCode::eLeft, InputAction::ePress);
    input.setKey(KeyCode::eLeft, InputAction::eRelease);
    input.addText("Y");
    click({40, 96});
    click({70, 145});
    click({600, 330});
    input.advanceFrame();
    step();
    require(gui.value("field") == "YZ", "Key/text order or event-time modifiers were lost");
    require(gui.isChecked("toggle") && changes == 1 && clicks == 1,
            "Same-frame clicks used the final mouse position or lost their transitions");
    require(probe.keyPresses == 1 && probe.textEvents == 0 && probe.mousePresses == 2,
            "UI ownership swallowed earlier game input or leaked UI input into the scene");
    require(world.input().action("fire").pressed && world.input().action("fire").released &&
                !world.input().action("activate").held && !world.input().action("modifier").pressed &&
                !input.isMouseButtonHeld(MouseCode::eLeft),
            "Consumed UI events erased action edges or left held input behind");
    require(gui.inputCapture().mouse && gui.inputCapture().keyboard,
            "Raw-state capture hints forgot UI-owned events after the pointer/focus left");
    const auto   composed = readback(device, target);
    const size_t outside  = (330 * size.width + 600) * 4;
    require(baseline.rgba[outside] < 0.2f && composed.rgba[outside] > 0.7f && composed.rgba[outside] < 0.8f,
            "UI changes did not reach scene logic and GPU output in the same frame");
    savePng(composed, run / "pipeline.png");
    input.advanceFrame();
    step();
    if (gui.inputCapture().mouse || gui.inputCapture().keyboard)
    {
        std::cerr << "Empty frame capture: mouse=" << gui.inputCapture().mouse
                  << ", keyboard=" << gui.inputCapture().keyboard << '\n';
    }
    require(!gui.inputCapture().mouse && !gui.inputCapture().keyboard,
            "An empty input frame replayed events or retained last frame's capture");

    input.setMousePosition({40, 96});
    input.setMouseButton(MouseCode::eLeft, true);
    input.advanceFrame();
    step();
    input.setFocused(false);
    input.advanceFrame();
    step();
    require(changes == 1 && !gui.inputCapture().mouse && !gui.inputCapture().keyboard,
            "Focus loss clicked the checkbox or retained UI capture");
    input.setFocused(true);
    input.setMousePosition({600, 330});
    input.advanceFrame();
    step();
    require(!gui.inputCapture().mouse, "A cancelled UI press resumed after focus returned");

    input.setMousePosition({40, 96});
    input.setMouseButton(MouseCode::eLeft, true);
    input.advanceFrame();
    step();
    input.setMouseButton(MouseCode::eLeft, false);
    input.advanceFrame();
    input.events()[0].consumed = true;
    step();
    require(changes == 1, "A release owned by another input layer completed a hidden UI click");

    click({50, 45});
    input.advanceFrame();
    step();
    input.addText("blocked");
    click({40, 96});
    input.advanceFrame();
    const auto keys  = probe.keyPresses;
    const auto mouse = probe.mousePresses;
    step({.mouse = true, .keyboard = true});
    require(gui.value("field") == "YZ" && changes == 1 && probe.keyPresses == keys && probe.mousePresses == mouse,
            "Higher-priority UI input leaked into VGui or the world");

    input.setMousePosition({380, 80});
    input.setMouseButton(MouseCode::eMiddle, true);
    input.setMouseButton(MouseCode::eMiddle, false);
    input.advanceFrame();
    step();
    require(gui.inputCapture().mouse, "The scroll area did not receive middle-button interaction");
    input.setFocused(false);
    input.advanceFrame();
    step();
    input.setFocused(true);
    input.setMousePosition({600, 330});
    input.advanceFrame();
    step();
    require(!gui.inputCapture().mouse && !gui.inputCapture().keyboard,
            "RmlUi autoscroll or focus survived cancellation");
    require(changes == 1 && clicks == 1, "Input cancellation fabricated a control activation");

    input.setMousePosition({40, 96});
    input.setMouseButton(MouseCode::eLeft, true);
    input.advanceFrame();
    step();
    input.advanceFrame();
    gui.processInput(input, {});
    gui.update({});
    require(!gui.inputCapture().mouse && !gui.inputCapture().keyboard && changes == 1,
            "An unrenderable UI retained capture or activated its pressed control");
    input.setMouseButton(MouseCode::eLeft, false);
    input.setMousePosition({600, 330});
    input.advanceFrame();
    step();
    // The release occurred over UI, so raw-state polling remains captured for this frame only.
    require(gui.inputCapture().mouse && !gui.inputCapture().keyboard && changes == 1 &&
                !input.isMouseButtonHeld(MouseCode::eLeft),
            "Restoring a renderable extent resumed a cancelled UI interaction");
    input.advanceFrame();
    step();
    require(!gui.inputCapture().mouse && !gui.inputCapture().keyboard && changes == 1,
            "A restored UI retained capture after its release frame");
    std::cout << "VGui input passed: event order, modifiers, ownership, cancellation and same-frame scene/GPU output; "
              << run << '\n';
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
