#include <vultra/scene/scene_tree.hpp>

#include <array>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

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

    template<class Function>
    void requireIdleRejection(Function&& function)
    {
        try
        {
            function();
        }
        catch (const std::logic_error&)
        {
            return;
        }
        throw std::runtime_error("Input callbacks allowed reentrant execution or direct structural mutation");
    }

    class TraceNode final : public Node
    {
    public:
        TraceNode(std::string name, std::vector<std::string>& trace) :
            Node(std::move(name)),
            m_Trace(trace)
        {
        }

        std::function<void(InputEvent&)> inputCallback;
        std::function<void(InputEvent&)> unhandledCallback;

        void onInput(InputEvent& event) override
        {
            m_Trace.push_back(name() + ".input");
            if (inputCallback)
            {
                inputCallback(event);
            }
        }

        void onUnhandledInput(InputEvent& event) override
        {
            m_Trace.push_back(name() + ".unhandled");
            if (unhandledCallback)
            {
                unhandledCallback(event);
            }
        }

    private:
        std::vector<std::string>& m_Trace;
    };

    class TraceBehavior final : public NodeBehavior
    {
    public:
        explicit TraceBehavior(std::vector<std::string>& trace) :
            m_Trace(trace)
        {
        }

        void input(InputEvent&) override
        {
            m_Trace.emplace_back("behavior.input");
        }

        void unhandledInput(InputEvent&) override
        {
            m_Trace.emplace_back("behavior.unhandled");
        }

    private:
        std::vector<std::string>& m_Trace;
    };

    InputEvent keyEvent(KeyCode code = KeyCode::eSpace, InputAction action = InputAction::ePress)
    {
        return {KeyInput {code, action}};
    }

    void orderedRouting()
    {
        std::vector<std::string> trace;
        TraceBehavior            behavior(trace);
        SceneTree                world(std::make_unique<TraceNode>("root", trace));
        auto&                    first = world.addChild(world.root(), std::make_unique<TraceNode>("first", trace));
        auto&                    child = world.addChild(first, std::make_unique<TraceNode>("child", trace));
        auto&                    last  = world.addChild(world.root(), std::make_unique<TraceNode>("last", trace));
        first.setProcessPriority(-100);
        last.setProcessPriority(100);
        world.attachBehavior(last, behavior);
        std::array events {keyEvent()};
        world.routeInput(events);
        require(trace.empty() && !world.running(), "Input implicitly started a stopped world");
        world.start();
        world.routeInput(events);
        const std::vector<std::string> expected {"last.input",
                                                 "behavior.input",
                                                 "child.input",
                                                 "first.input",
                                                 "root.input",
                                                 "last.unhandled",
                                                 "behavior.unhandled",
                                                 "child.unhandled",
                                                 "first.unhandled",
                                                 "root.unhandled"};
        require(trace == expected, "Input order is not reverse tree order or behavior order changed");

        trace.clear();
        last.inputCallback = [](InputEvent& event)
        {
            event.consumed = true;
        };
        world.routeInput(events);
        require(events[0].consumed && trace == std::vector<std::string> {"last.input"},
                "Consumption did not stop the node's behavior and later phases");

        trace.clear();
        events[0].consumed      = false;
        last.inputCallback      = {};
        child.unhandledCallback = [](InputEvent& event)
        {
            event.consumed = true;
        };
        world.routeInput(events);
        const std::vector<std::string> consumed {"last.input",
                                                 "behavior.input",
                                                 "child.input",
                                                 "first.input",
                                                 "root.input",
                                                 "last.unhandled",
                                                 "behavior.unhandled",
                                                 "child.unhandled"};
        require(events[0].consumed && trace == consumed, "Unhandled consumption leaked to later nodes");
        world.stop();
        world.detachBehavior(last, behavior);
    }

    void ownershipAndPause()
    {
        std::vector<std::string> trace;
        TraceBehavior            behavior(trace);
        SceneTree                world(std::make_unique<TraceNode>("root", trace));
        auto&                    always = world.addChild(world.root(), std::make_unique<TraceNode>("always", trace));
        auto&                    paused = world.addChild(world.root(), std::make_unique<TraceNode>("paused", trace));
        auto& disabled                  = world.addChild(world.root(), std::make_unique<TraceNode>("disabled", trace));
        always.setProcessMode(ProcessMode::eAlways);
        paused.setProcessMode(ProcessMode::eWhenPaused);
        disabled.setProcessMode(ProcessMode::eDisabled);
        world.attachBehavior(always, behavior);
        world.start();

        std::array captured {keyEvent(),
                             InputEvent {TextInput {"text"}},
                             InputEvent {MouseMotionInput {{1, 2}}},
                             InputEvent {MouseButtonInput {MouseCode::eLeft, true}},
                             InputEvent {MouseScrollInput {{0, 1}}},
                             InputEvent {FocusInput {false}},
                             keyEvent()};
        captured.back().consumed = true;
        world.routeInput(captured, {.mouse = true, .keyboard = true});
        const std::vector<std::string> focus {"always.input",
                                              "behavior.input",
                                              "root.input",
                                              "always.unhandled",
                                              "behavior.unhandled",
                                              "root.unhandled"};
        require(trace == focus && !captured[0].consumed, "UI capture routed owned events or suppressed focus events");

        trace.clear();
        world.setPaused(true);
        always.setInputEnabled(false);
        paused.setUnhandledInputEnabled(false);
        std::array events {keyEvent()};
        world.routeInput(events);
        require(trace == std::vector<std::string> {"paused.input", "always.unhandled", "behavior.unhandled"},
                "Input ignored pause modes or independent runtime routing switches");
        trace.clear();
        always.setInputEnabled(true);
        always.inputCallback = [&](InputEvent&)
        {
            always.setProcessMode(ProcessMode::eDisabled);
        };
        world.routeInput(events);
        require(trace == std::vector<std::string> {"paused.input", "always.input"},
                "A node disabled by its callback still called its behavior or unhandled phase");

        world.stop();
        world.detachBehavior(always, behavior);
        auto detached = world.remove(paused);
        require(!detached->unhandledInputEnabled(), "Detaching reset the runtime input switches");
        auto& restored = world.addChild(world.root(), std::move(detached));
        require(!restored.unhandledInputEnabled(), "Attaching reset the runtime input switches");
        const auto encoded = world.serialize();
        require(encoded.find("inputEnabled") == std::string::npos &&
                    encoded.find("unhandledInput") == std::string::npos,
                "Scene persistence included runtime input switches");
    }

    void deferredChanges()
    {
        std::vector<std::string> trace;
        SceneTree                world(std::make_unique<TraceNode>("root", trace));
        auto&                    first = world.addChild(world.root(), std::make_unique<TraceNode>("first", trace));
        world.addChild(first, std::make_unique<TraceNode>("child", trace));
        auto&      last    = world.addChild(world.root(), std::make_unique<TraceNode>("last", trace));
        const auto removed = first.id();
        bool       changed = false;
        last.inputCallback = [&](InputEvent&)
        {
            if (!changed)
            {
                changed = true;
                world.queueFree(removed);
                world.deferAddChild(world.root().id(), std::make_unique<TraceNode>("new", trace));
            }
        };
        world.start();
        std::array events {keyEvent(KeyCode::eA), keyEvent(KeyCode::eB)};
        world.routeInput(events);
        const std::vector<std::string> expected {"last.input",
                                                 "root.input",
                                                 "last.unhandled",
                                                 "root.unhandled",
                                                 "new.input",
                                                 "last.input",
                                                 "root.input",
                                                 "new.unhandled",
                                                 "last.unhandled",
                                                 "root.unhandled"};
        require(trace == expected && !world.find(removed),
                "Deferred changes crossed an event boundary or queued descendants still received input");
    }

    void callbackBoundaries()
    {
        std::vector<std::string> trace;
        SceneTree                world(std::make_unique<TraceNode>("root", trace));
        auto&                    root = static_cast<TraceNode&>(world.root());
        root.inputCallback            = [&](InputEvent&)
        {
            requireIdleRejection(
                [&]
                {
                    world.routeInput({});
                });
            requireIdleRejection(
                [&]
                {
                    world.advance(0);
                });
            requireIdleRejection(
                [&]
                {
                    world.physicsStep();
                });
            requireIdleRejection(
                [&]
                {
                    world.addChild(root, std::make_unique<Node>("invalid"));
                });
        };
        world.start();
        std::array events {keyEvent(), keyEvent()};
        world.routeInput(events);
        require(world.running() && !world.dispatching() && root.children().empty(),
                "Rejected reentrant input left the world dispatching or mutated it");

        trace.clear();
        root.inputCallback = [&](InputEvent&)
        {
            world.deferAddChild(root.id(), std::make_unique<Node>("not published"));
            throw std::runtime_error("injected input failure");
        };
        bool failed = false;
        try
        {
            world.routeInput(events);
        }
        catch (const std::runtime_error& error)
        {
            failed = std::string_view(error.what()) == "injected input failure";
        }
        require(failed && !world.running() && !world.dispatching() && root.children().empty(),
                "Input failure was swallowed or retained deferred work/active execution");
        root.inputCallback = {};
        world.start();
        trace.clear();
        world.routeInput(events);
        require(trace.size() == 4 && root.children().empty(), "Input did not recover after a callback failure");

        trace.clear();
        root.inputCallback = [&](InputEvent&)
        {
            world.stop();
        };
        world.routeInput(events);
        require(!world.running() && !world.dispatching() && trace == std::vector<std::string> {"root.input"},
                "A stop request allowed more callbacks or restarted for the next input event");
    }

    void eventActions()
    {
        Input    input;
        InputMap map;
        map.bind("jump", KeyCode::eSpace);
        map.bind("jump", MouseCode::eRight);
        input.setKey(KeyCode::eSpace, InputAction::ePress);
        input.setKey(KeyCode::eSpace, InputAction::eRepeat);
        input.addText("hello");
        input.setKey(KeyCode::eSpace, InputAction::eRelease);
        input.setMouseButton(MouseCode::eRight, true);
        input.setMouseButton(MouseCode::eRight, false);
        input.advanceFrame();
        map.update(input.events());
        const auto events = input.events();
        require(map.action("jump").pressed && map.action("jump").released, "Frame edges did not aggregate input");
        const auto press        = map.eventAction("jump", events[0]);
        const auto repeat       = map.eventAction("jump", events[1]);
        const auto text         = map.eventAction("jump", events[2]);
        const auto release      = map.eventAction("jump", events[3]);
        const auto mousePress   = map.eventAction("jump", events[4]);
        const auto mouseRelease = map.eventAction("jump", events[5]);
        require(press.held && press.pressed && !press.released && repeat.held && !repeat.pressed && !repeat.released &&
                    !text.held && !text.pressed && !text.released && !release.held && !release.pressed &&
                    release.released && mousePress.held && mousePress.pressed && mouseRelease.released &&
                    !mouseRelease.held,
                "Per-event actions leaked frame edges or lost repeat/mouse semantics");
        events[0].consumed = true;
        require(map.eventAction("jump", events[0]).pressed, "Raw action matching changed when ownership changed");
        bool rejected = false;
        try
        {
            map.eventAction("missing", events[0]);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(rejected, "Per-event matching silently accepted an unknown action");
    }
} // namespace

int main()
try
{
    orderedRouting();
    ownershipAndPause();
    deferredChanges();
    callbackBoundaries();
    eventActions();
    std::cout << "Scene input tests passed: routing, ownership, pause, deferred changes and failure/recovery\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
