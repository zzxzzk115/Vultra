#include <vultra/api/scene_bridge.hpp>
#include <vultra/assets/source_file.hpp>
#include <vultra/platform/os/file.hpp>
#include <vultra/scene/node_3d.hpp>
#include <vultra/scene/scene_tree.hpp>
#include <vultra/scripting/script_host.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
    using namespace vultra;

    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    class ApiProbe final : public Node
    {
    public:
        ApiProbe() :
            Node("Input ABI probe")
        {
        }

        SceneAccess          access;
        VultraSceneFrame     saved {};
        VultraSceneInputData previous {};
        std::string          text;

        void onInput(InputEvent& event) override
        {
            access.scene  = tree();
            access.active = true;
            const VultraSceneFrame frame {&access, ++access.serial};
            const auto&            api = sceneApi();
            VultraSceneInputData   value {};
            require(api.input_event(frame, &value) == VULTRA_STATUS_OK && value.serial > previous.serial,
                    "Input ABI lost the active event or reused its serial");
            if (previous.serial)
            {
                uint64_t state = 0;
                require(api.set_input_handled(frame, previous.serial) == VULTRA_STATUS_INVALID_ARGUMENT &&
                            api.input_action_state(saved, previous.serial, "jump", 4, &state) ==
                                VULTRA_STATUS_INVALID_FRAME,
                        "A retained input event or callback frame addressed a later event");
            }
            const char* bytes = nullptr;
            uint64_t    size  = 0;
            require(api.input_text(frame, value.serial, &bytes, &size) == VULTRA_STATUS_OK, "Input text borrow failed");
            text.append(bytes ? bytes : "", size);
            if (const auto* key = std::get_if<KeyInput>(&event.value))
            {
                uint64_t state = 0;
                require(value.kind == VULTRA_INPUT_EVENT_KIND_KEY && value.code == uint32_t(key->key) &&
                            value.modifiers == key->modifiers && value.action == uint32_t(key->action) &&
                            api.input_action_state(frame, value.serial, "jump", 4, &state) == VULTRA_STATUS_OK &&
                            state == 3,
                        "Input ABI changed a typed key/action/modifier snapshot");
            }
            require(api.set_input_handled(frame, value.serial) == VULTRA_STATUS_OK && event.consumed,
                    "Input ABI did not consume the actual raw event");
            previous      = value;
            saved         = frame;
            access.active = false;
        }
    };

    void borrowedAbi()
    {
        SceneTree world(std::make_unique<ApiProbe>());
        world.input().bind("jump", KeyCode::eSpace);
        Input input;
        input.setKey(KeyCode::eSpace, InputAction::ePress);
        input.addText("héλ");
        input.advanceFrame();
        world.advance(0, input.events());
        auto&                probe = static_cast<ApiProbe&>(world.root());
        VultraSceneInputData value {};
        require(probe.text == "héλ" && sceneApi().input_event(probe.saved, &value) == VULTRA_STATUS_INVALID_FRAME,
                "Text was not copied during its borrow or the ABI frame survived callback return");
        probe.access.active = true;
        require(sceneApi().input_event(probe.saved, &value) == VULTRA_STATUS_INVALID_ARGUMENT,
                "An active non-input callback exposed an expired input event");
        probe.access.active = false;
    }

    std::filesystem::path
    run(ScriptLanguage language, const std::filesystem::path& binaries, const std::filesystem::path& directory)
    {
        namespace fs = std::filesystem;
        fs::create_directories(directory);
        const bool managed = language == ScriptLanguage::eCSharp;
        fs::path   path;
        if (language == ScriptLanguage::eLua)
        {
            path = directory / "input.lua";
            fs::copy_file("tests/scene_scripts/input.lua", path);
        }
        else if (managed)
        {
            for (const auto& file : fs::directory_iterator("build/.tmp/scripting-managed"))
            {
                if (file.is_regular_file())
                {
                    fs::copy_file(file.path(), directory / file.path().filename());
                }
            }
            path = directory / "Vultra.SceneScripts.dll";
        }
        else
        {
#ifdef _WIN32
            const auto source = binaries / "test-native-input-script.dll";
#else
            const auto source = binaries / "libtest-native-input-script.so";
#endif
            path = directory / source.filename();
            fs::copy_file(source, path);
        }
        SceneTree  world(std::make_unique<Node>("Input world"));
        auto&      actor = world.addChild(world.root(), std::make_unique<Node3D>("Input actor"));
        const auto id    = actor.id();
        world.input().bind("jump", KeyCode::eSpace);
        ScriptHost host(world, true);
        host.add(
            {language, path, managed ? "VultraTests.InputController" : "InputController", actor.idInScene().value});
        require(host.scriptClass(id).input && host.scriptClass(id).unhandledInput,
                "Input callback metadata lost an inherited or declared handler");
        const auto field = [managed](std::string name)
        {
            if (managed)
            {
                name[0] = char(name[0] - 'a' + 'A');
            }
            return name;
        };
        const auto count = [&](std::string name)
        {
            return std::get<int64_t>(host.readExport(id, field(std::move(name))));
        };
        Input      input;
        const auto step = [&](InputCapture capture = {})
        {
            input.advanceFrame();
            world.input().update(input.events(), capture);
            host.update(0, input.events(), capture);
        };
        input.setMousePosition({10, 20});
        input.setKey(KeyCode::eSpace, InputAction::ePress);
        input.addText("héλ");
        input.setMouseButton(MouseCode::eLeft, true);
        input.setMousePosition({13, 24});
        input.addMouseScroll({0, -2});
        input.setKey(KeyCode::eSpace, InputAction::eRelease);
        step();
        require(count("inputs") == 7 && count("presses") == 1 && count("unhandled") == 6 && count("expired") == 7 &&
                    count("updates") == 1 && host.readExport(id, field("typed")) == PropertyValue(std::string("héλ")),
                "Language input order, consumption, text, per-event actions or stale-borrow rejection differs");
        require(actor.localTransform()[3].x == 1 && actor.localTransform()[3].y == 2 &&
                    std::abs(actor.localTransform()[3].z - 0.53f) < 0.000001f && input.events()[1].consumed,
                "Script input did not drive its actual transform or consume the source event");

        world.setPaused(true);
        input.setKey(KeyCode::eSpace, InputAction::ePress);
        step();
        require(count("inputs") == 7 && count("updates") == 1, "Paused scripts still received input/process callbacks");
        world.setPaused(false);
        input.setKey(KeyCode::eSpace, InputAction::eRelease);
        input.setKey(KeyCode::eSpace, InputAction::ePress);
        step({.keyboard = true});
        require(count("inputs") == 7 && count("updates") == 2 && input.isKeyHeld(KeyCode::eSpace) &&
                    !world.input().action("jump").held,
                "UI capture leaked input, erased raw state or retained a captured action");

        input.setKey(KeyCode::eF2, InputAction::ePress);
        input.setKey(KeyCode::eSpace, InputAction::eRelease);
        input.setKey(KeyCode::eSpace, InputAction::ePress);
        step();
        require(count("inputs") == 8 && count("unhandled") == 9 && count("presses") == 1 && !actor.inputEnabled(),
                "Independent input switches failed to preserve only the unhandled phase");
        actor.setInputEnabled(true);
        input.setKey(KeyCode::eF3, InputAction::ePress);
        input.setMouseButton(MouseCode::eLeft, false);
        input.setMouseButton(MouseCode::eLeft, true);
        step();
        require(count("inputs") == 11 && count("unhandled") == 9 && actor.localTransform()[3].y == 2 &&
                    !actor.unhandledInputEnabled(),
                "Disabling unhandled input also disabled input or still applied mouse logic");

        const auto        contents = readSourceFile(path);
        const auto        stamp    = fs::last_write_time(path);
        const std::string invalid  = "invalid input module";
        writeFileAtomically(path, std::as_bytes(std::span(invalid)));
        fs::last_write_time(path, stamp + std::chrono::seconds(2));
        require(host.reloadChanged() == 0 && !host.lastReloadError().empty(),
                "Invalid input script replaced the active class");
        input.setKey(KeyCode::eSpace, InputAction::eRelease);
        input.setKey(KeyCode::eSpace, InputAction::ePress);
        step();
        require(actor.localTransform()[3].x == 2, "Failed replacement broke active input logic");
        writeFileAtomically(path, contents);
        fs::last_write_time(path, stamp + std::chrono::seconds(4));
        require(host.reloadChanged(true) == 1 && host.scriptClass(id).input && host.scriptClass(id).unhandledInput,
                "Recovered input script lost its generated callbacks");
        input.setKey(KeyCode::eSpace, InputAction::eRelease);
        input.setKey(KeyCode::eSpace, InputAction::ePress);
        step();
        require(actor.localTransform()[3].x == 3, "Recovered input script did not drive the existing entity");
        input.setKey(KeyCode::eDelete, InputAction::ePress);
        step();
        require(!world.find(id), "Input-time queueFree did not retire the script's entity safely");
        host.stop();
        world.routeInput(input.events());
        require(!world.running(), "Input restarted a stopped script world");
        return path;
    }

    void sharedWorld(const std::array<std::filesystem::path, 3>& paths)
    {
        const std::array       languages {ScriptLanguage::eNative, ScriptLanguage::eLua, ScriptLanguage::eCSharp};
        SceneTree              world(std::make_unique<Node>("Shared input world"));
        std::array<Node3D*, 3> actors {};
        for (size_t i = actors.size(); i-- > 0;)
        {
            actors[i] = &world.addChild(world.root(), std::make_unique<Node3D>("Actor " + std::to_string(i)));
        }
        world.input().bind("jump", KeyCode::eSpace);
        ScriptHost                      host(world);
        std::array<ScriptAttachment, 3> attachments;
        for (size_t i = 0; i < attachments.size(); ++i)
        {
            attachments[i] = {languages[i],
                              paths[i],
                              i == 2 ? "VultraTests.InputController" : "InputController",
                              actors[i]->idInScene().value};
        }
        host.add(attachments);
        Input input;
        for (size_t winner = 0; winner < actors.size(); ++winner)
        {
            if (winner > 0)
            {
                actors[winner - 1]->setInputEnabled(false);
            }
            input.setKey(KeyCode::eSpace, InputAction::eRelease);
            input.setKey(KeyCode::eSpace, InputAction::ePress);
            input.advanceFrame();
            world.input().update(input.events());
            host.update(0, input.events());
            for (size_t i = 0; i < actors.size(); ++i)
            {
                require(actors[i]->localTransform()[3].x == (i <= winner ? 1.0f : 0.0f),
                        "Cross-language input order or consumption differs in one shared world");
            }
            require(input.events().back().consumed, "A shared-world script failed to consume the source event");
        }
    }
} // namespace

int main(int argc, char** argv)
try
{
    require(argc > 0, "Missing executable path");
    borrowedAbi();
    const auto root = std::filesystem::absolute("build/.tmp/script-input-" + StableId::generate().toString());
    std::array<std::filesystem::path, 3> paths;
    size_t                               index = 0;
    for (const auto language : {ScriptLanguage::eNative, ScriptLanguage::eLua, ScriptLanguage::eCSharp})
    {
        paths[index++] =
            run(language, std::filesystem::absolute(argv[0]).parent_path(), root / std::to_string(int(language)));
    }
    sharedWorld(paths);
    std::cout << "Native/Lua/C# input, transforms, typed snapshots, borrow expiry and replacement recovery passed\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
