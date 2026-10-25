#include <vultra/api/vultra_abi.generated.h>
#include <vultra/scene/node_3d.hpp>
#include <vultra/scene/scene_tree.hpp>
#include <vultra/scripting/script_host.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

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

    void write(const std::filesystem::path& path, std::string_view text)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream << text;
        require(bool(stream), "Write Lua context fixture");
    }

    std::string modulePath(const std::filesystem::path& directory)
    {
        return "package.path = " + nlohmann::json((directory / "?.lua").generic_string() + ";").dump() +
               " .. package.path\n";
    }

    std::string probe(const std::filesystem::path& directory, int revision)
    {
        return modulePath(directory) + "local revision = " + std::to_string(revision) + R"lua(
private_ticks = 0
_G.private_ticks = 0
local shared = require('shared_state')
local Script = {
    class_name = 'ContextProbe', extends = 'Node3D',
    exports = {
        ticks = { kind='int', integer_bits=32, default=0 },
        sharedTicks = { kind='int', integer_bits=32, default=0 },
        revision = { kind='int', integer_bits=32, default=revision },
        staleRejected = { kind='bool', default=false }
    }
}
function Script:_ready()
    if self.node:name() == 'Publisher' then
        shared.cached = self.scene:root():get_child(1)
        local view = shared.cached
        shared.action = function()
            local x, y, z = view:position()
            view:set_position(x, 10, z)
        end
    end
end
function Script:_process(delta)
    if self.node:name() == 'Observer' then
        self.staleRejected = not pcall(shared.action)
        return
    end
    _G.private_ticks = _G.private_ticks + 1
    private_ticks = private_ticks + 1
    assert(private_ticks == _G.private_ticks)
    self.ticks = private_ticks
    shared.counter = shared.counter + 1
    self.sharedTicks = shared.counter
    self.node:set_position(private_ticks, 0, 0)
    if self.node:name() == 'Survivor' then shared.action() end
end
return Script
)lua";
    }

    void attach(ScriptHost& host, const std::filesystem::path& path, const Node& node, std::string_view type)
    {
        host.add({ScriptLanguage::eLua, path, std::string(type), node.idInScene().value});
    }

    void sharedModules(const std::filesystem::path& directory)
    {
        const auto marker = directory / "finalized.txt";
        write(directory / "shared_state.lua",
              "local marker = " + nlohmann::json(marker.generic_string()).dump() + R"lua(
local state = { counter = 0 }
state.sentinel = setmetatable({}, { __gc = function()
    local ok, message = pcall(function() return state.cached:name() end)
    local file = assert(io.open(marker, 'a'))
    file:write(ok and 'unexpected live frame\n' or tostring(message) .. '\n')
    file:close()
end })
return state
)lua");
        const auto path = directory / "probe.lua";
        write(path, probe(directory, 1));
        const auto stamp = std::filesystem::last_write_time(path);
        SceneTree  world(std::make_unique<Node>("Lua world"));
        auto&      publisher = world.addChild(world.root(), std::make_unique<Node3D>("Publisher"));
        auto&      survivor  = world.addChild(world.root(), std::make_unique<Node3D>("Survivor"));
        auto&      observer  = world.addChild(world.root(), std::make_unique<Node3D>("Observer"));
        ScriptHost host(world, true);
        for (const auto* node : {&publisher, &survivor, &observer})
        {
            attach(host, path, *node, "ContextProbe");
        }
        auto&      invalid = world.addChild(world.root(), std::make_unique<Node3D>("Invalid"));
        const auto badPath = directory / "invalid_schema.lua";
        write(badPath,
              modulePath(directory) +
                  "local shared=require('shared_state'); return { class_name='Invalid', extends='Node3D', "
                  "exports={speed={kind='float',default='wrong'}} }\n");
        bool rejected = false;
        try
        {
            attach(host, badPath, invalid, "Invalid");
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(rejected && !world.registry().get<SceneProcess>(invalid.entity()).behavior,
                "Invalid export metadata attached behavior or did not report a schema failure");
        host.update(0.01f);
        require(host.readExport(publisher.id(), "ticks") == PropertyValue(int64_t {2}) &&
                    host.readExport(survivor.id(), "ticks") == PropertyValue(int64_t {2}) &&
                    host.readExport(publisher.id(), "sharedTicks") == PropertyValue(int64_t {1}) &&
                    host.readExport(survivor.id(), "sharedTicks") == PropertyValue(int64_t {2}) &&
                    survivor.localTransform()[3].y == 10,
                "Lua instances share ordinary globals or do not share require modules/object views");

        // A separate world's package cache must remain independent while the first VM is still live.
        {
            SceneTree  isolated(std::make_unique<Node>("Other world"));
            auto&      first  = isolated.addChild(isolated.root(), std::make_unique<Node3D>("Publisher"));
            auto&      second = isolated.addChild(isolated.root(), std::make_unique<Node3D>("Survivor"));
            ScriptHost other(isolated);
            attach(other, path, first, "ContextProbe");
            attach(other, path, second, "ContextProbe");
            other.update(0.01f);
            require(other.readExport(second.id(), "sharedTicks") == PropertyValue(int64_t {2}) &&
                        host.readExport(survivor.id(), "sharedTicks") == PropertyValue(int64_t {2}),
                    "Lua package cache leaked between worlds");
        }
        const auto publisherId      = publisher.id();
        auto       retiredPublisher = world.remove(publisher);
        retiredPublisher.reset();
        host.update(0.01f);
        require(!world.find(publisherId) && survivor.localTransform()[3].y == 10 &&
                    host.readExport(survivor.id(), "sharedTicks") == PropertyValue(int64_t {3}),
                "Cached node/API closure depended on its destroyed originating script");
        write(path, "local broken = ");
        std::filesystem::last_write_time(path, stamp + std::chrono::seconds(2));
        require(host.reloadChanged() == 0 && !host.lastReloadError().empty(), "Broken Lua replacement was activated");
        host.update(0.01f);
        require(host.readExport(survivor.id(), "sharedTicks") == PropertyValue(int64_t {4}) &&
                    survivor.localTransform()[3].y == 10,
                "Failed staging damaged the shared VM or the preceding script");
        write(path, probe(directory, 2));
        std::filesystem::last_write_time(path, stamp + std::chrono::seconds(4));
        require(host.reloadChanged() == 2 && host.lastReloadError().empty(), "Valid Lua replacement did not recover");
        host.update(0.01f);
        require(host.readExport(survivor.id(), "revision") == PropertyValue(int64_t {2}) &&
                    host.readExport(survivor.id(), "ticks") == PropertyValue(int64_t {2}) &&
                    host.readExport(survivor.id(), "sharedTicks") == PropertyValue(int64_t {5}) &&
                    survivor.localTransform()[3].y == 10,
                "Replacement did not isolate new locals or retain the world's module cache");
        const auto staleId         = survivor.id();
        const auto authored        = survivor.idInScene();
        auto       retiredSurvivor = world.remove(survivor);
        retiredSurvivor.reset();
        auto& successor = world.addChild(world.root(), std::make_unique<Node3D>("Successor", authored));
        host.update(0.01f);
        require(successor.id() != staleId && host.readExport(observer.id(), "staleRejected") == PropertyValue(true) &&
                    successor.localTransform()[3] == glm::vec4(0, 0, 0, 1),
                "A cached Lua view revived a retired object or altered its successor");
        host.stop();
        std::ifstream     file(marker);
        const std::string finalizers((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        const auto        expected = "Vultra API status " + std::to_string(int(VULTRA_STATUS_INVALID_ARGUMENT));
        const auto        first    = finalizers.find(expected);
        require(first != std::string::npos && finalizers.find(expected, first + expected.size()) != std::string::npos &&
                    finalizers.find("unexpected live frame") == std::string::npos,
                "Lua finalization retained an active scene frame or did not close both contexts");
    }

    void sharedClassInstances(const std::filesystem::path& directory)
    {
        write(directory / "shared_class.lua", R"lua(
local Script = { class_name='SharedClass', extends='Node3D', exports={speed={kind='float',default=1}} }
function Script:_process(delta) self.node:set_position(self.speed, 0, 0) end
return Script
)lua");
        const auto path = directory / "shared_entry.lua";
        write(path, modulePath(directory) + "return require('shared_class')\n");
        SceneTree        world(std::make_unique<Node>("Shared class"));
        auto&            first  = world.addChild(world.root(), std::make_unique<Node3D>("First"));
        auto&            second = world.addChild(world.root(), std::make_unique<Node3D>("Second"));
        ScriptHost       host(world);
        const std::array firstValues {ScriptExport {"speed", 2.0f}};
        const std::array secondValues {ScriptExport {"speed", 3.0f}};
        host.add({ScriptLanguage::eLua, path, "SharedClass", first.idInScene().value, firstValues});
        host.add({ScriptLanguage::eLua, path, "SharedClass", second.idInScene().value, secondValues});
        host.update(0.01f);
        require(first.localTransform()[3].x == 2 && second.localTransform()[3].x == 3,
                "Shared class declarations aliased instance export/attachment storage");
    }

    void callbackFailure(const std::filesystem::path& directory)
    {
        const auto path = directory / "callback_failure.lua";
        write(path, R"lua(
return setmetatable({ class_name='CallbackProbe', extends='Node3D' }, {
    __index = function(class, name)
        if name == '_process' then error('callback lookup failure') end
    end
})
)lua");
        const auto stamp = std::filesystem::last_write_time(path);
        SceneTree  world(std::make_unique<Node3D>("Callback failure"));
        ScriptHost host(world, true);
        attach(host, path, world.root(), "CallbackProbe");
        bool failed = false;
        try
        {
            host.update(0.01f);
        }
        catch (const std::runtime_error& error)
        {
            failed = std::string_view(error.what()).find("callback lookup failure") != std::string_view::npos;
        }
        require(failed && !world.running(), "Callback-table lookup escaped protected execution");
        write(path, R"lua(
local Script = { class_name='CallbackProbe', extends='Node3D' }
function Script:_process(delta) self.node:set_position(7, 0, 0) end
return Script
)lua");
        std::filesystem::last_write_time(path, stamp + std::chrono::seconds(2));
        require(host.reloadChanged() == 1 && host.lastReloadError().empty(), "Callback failure poisoned replacement");
        host.update(0.01f);
        require(static_cast<const Node3D&>(world.root()).localTransform()[3].x == 7,
                "Callback failure retained a broken Lua stack or frame");
    }
} // namespace

int main()
try
{
    const auto directory = std::filesystem::absolute("build/.tmp") / ("lua-context-" + StableId::generate().toString());
    std::filesystem::create_directories(directory);
    sharedModules(directory);
    sharedClassInstances(directory);
    callbackFailure(directory);
    std::cout << "Lua context passed: shared modules, isolated instances/worlds, cached views, reload and teardown\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
