#include <vultra/scene/node_3d.hpp>
#include <vultra/scene/scene_tree.hpp>
#include <vultra/scripting/script_host.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
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

    void write(const std::filesystem::path& path, std::string_view text)
    {
        std::ofstream file(path);
        file << text;
        file.close();
        require(bool(file), "Write Lua module fixture");
    }

    std::string pathPrelude(const std::filesystem::path& directory)
    {
        return "package.path=" + nlohmann::json((directory / "?.lua").generic_string() + ";").dump() +
               "..package.path\n";
    }

    std::string actor(const std::filesystem::path& directory, bool shadow = false, bool late = false)
    {
        return pathPrelude(directory) +
               "local Ship=require('actors.ship');local shared=require('support.state')\n"
               "local cycle=require('support.cycle_a');assert(cycle.other.a==cycle)\n" +
               std::string(shadow ? "local leaked=require('support.leaked')\n" : "") +
               std::string(late ? "local late=require('support.late')\n" : "") +
               "local Script={class_name='Scout',extends=Ship,exports={height={kind='float',default=3},"
               "sharedCount={kind='int',default=0},leaked={kind='bool',default=false}" +
               std::string(shadow ? ",speed={kind='float',default=9}" : "") + R"lua(}}
function Script:_process(delta)
    Ship._process(self,delta)
    local x,y,z=self.node:position()
    shared.calls=shared.calls+1;self.sharedCount=shared.calls
    self.leaked=package.loaded['support.leaked']~=nil
    self.node:set_position(x,y,self.height)
end
return Script
)lua";
    }

    void inheritanceAndModules(const std::filesystem::path& directory)
    {
        std::filesystem::create_directories(directory / "actors");
        std::filesystem::create_directories(directory / "support");
        const auto root   = directory / "scout.lua";
        const auto base   = directory / "actors/mover.lua";
        const auto helper = directory / "support/multiplier.lua";
        const auto other  = directory / "other.lua";
        const auto late   = directory / "support/late.lua";
        write(helper, "return {value=1}\n");
        write(directory / "support/state.lua", "return {calls=0}\n");
        write(directory / "support/role.lua", "return {class_name='Role',extends='Node3D'}\n");
        write(directory / "support/leaked.lua", "return {}\n");
        write(late, "return {value=1}\n");
        write(directory / "support/energy.lua", R"lua(
local multiplier=require('support.multiplier')
return {sample=function(speed) return speed*multiplier.value end}
)lua");
        write(directory / "support/cycle_a.lua", R"lua(
local value={};package.loaded['support.cycle_a']=value
value.other=require('support.cycle_b');return value
)lua");
        write(directory / "support/cycle_b.lua", "return {a=require('support.cycle_a')}\n");
        write(base, R"lua(
local energy=require('support.energy')
local Script={class_name='Mover',extends='Node3D',exports={
    speed={kind='float',default=2},readyCount={kind='int',default=0},
    target={kind='reference',reference={category='node',type='Node3D',class={name='Mover'}}},
    relative={kind='reference',reference={category='node',type='Node3D',
        class={module='../support/role.lua',name='Role'}}}
}}
function Script:_ready() self.readyCount=self.readyCount+1 end
function Script:_process(delta)
    local current=require('support.energy')
    assert(current==energy)
    self.node:set_position(current.sample(self.speed)*delta,self.readyCount,0)
end
return Script
)lua");
        write(directory / "actors/ship.lua", R"lua(
local Base=require('actors.mover')
local Script={class_name='Ship',extends=Base,exports={boost={kind='float',default=2}}}
function Script:_process(delta)
    Base._process(self,delta)
    local x,y,z=self.node:position();self.node:set_position(x*self.boost,y,z)
end
return Script
)lua");
        const auto otherSource = pathPrelude(directory) + R"lua(
local late=require('support.late')
local Script={class_name='Other',extends='Node3D',exports={gain={kind='float',default=1}}}
function Script:_process(delta)
    local current=require('support.late');assert(current==late)
    self.node:set_position(current.value*self.gain*delta,0,0)
end
return Script
)lua";
        write(other, otherSource);
        write(root, actor(directory));
        SceneTree  world(std::make_unique<Node>("Lua inheritance"));
        const auto node = [&](const char* name) -> Node3D&
        {
            return static_cast<Node3D&>(world.addChild(world.root(), std::make_unique<Node3D>(name)));
        };
        auto&            first       = node("First");
        auto&            second      = node("Second");
        auto&            independent = node("Independent");
        ScriptHost       host(world, true);
        const std::array firstValues {ScriptExport {"speed", 3.0f},
                                      ScriptExport {"target", PropertyReference {second.idInScene().value}}};
        const std::array secondValues {ScriptExport {"speed", 5.0f}};
        const std::array attachments {
            ScriptAttachment {ScriptLanguage::eLua, root, "Scout", first.idInScene().value, firstValues},
            ScriptAttachment {ScriptLanguage::eLua, root, "Scout", second.idInScene().value, secondValues},
            ScriptAttachment {ScriptLanguage::eLua, other, "Other", independent.idInScene().value}};
        host.add(attachments);
        const auto& type = host.scriptClass(first.id());
        require(type.classes.size() == 3 && type.classes[1].name == "Ship" && type.classes[2].name == "Mover" &&
                    type.classes[2].module == std::filesystem::canonical(base).generic_string() &&
                    type.property("target").referenceClass->module ==
                        std::filesystem::canonical(base).generic_string() &&
                    type.property("relative").referenceClass->module ==
                        std::filesystem::canonical(directory / "support/role.lua").generic_string(),
                "Lua inherited classes or declaring-module reference origins are wrong");
        require(std::ranges::find(type.dependencies, std::filesystem::canonical(helper).generic_string()) !=
                    type.dependencies.end(),
                "Transitive Lua helper dependencies were not reported");
        host.update(0.25f);
        require(first.localTransform()[3] == glm::vec4(1.5f, 1, 3, 1) && second.localTransform()[3].x == 2.5f &&
                    host.readExport(second.id(), "sharedCount") == PropertyValue(int64_t {2}),
                "Lua inheritance, inherited callbacks or world-owned shared modules did not execute");
        host.writeExport(first.id(), "speed", 7.0f);
        const auto* oldFirst       = world.registry().get<SceneProcess>(first.entity()).behavior;
        const auto* oldIndependent = world.registry().get<SceneProcess>(independent.entity()).behavior;
        const auto  helperStamp    = std::filesystem::last_write_time(helper);
        const auto  rootStamp      = std::filesystem::last_write_time(root);
        write(helper, "local broken = ");
        std::filesystem::last_write_time(helper, helperStamp + std::chrono::seconds(2));
        require(host.reloadChanged() == 0 && !host.lastReloadError().empty() && host.reloadChanged() == 0,
                "A broken helper was activated or repeatedly retried");
        host.update(0.25f);
        require(first.localTransform()[3].x == 3.5f && host.readExport(first.id(), "speed") == PropertyValue(7.0f),
                "Failed Lua helper staging changed the active exports or package cache");
        write(helper, "return {value=2}\n");
        std::filesystem::last_write_time(helper, helperStamp + std::chrono::seconds(4));
        write(root, actor(directory, true));
        std::filesystem::last_write_time(root, rootStamp + std::chrono::seconds(2));
        require(host.reloadChanged() == 0 && world.registry().get<SceneProcess>(first.entity()).behavior == oldFirst,
                "A shadowed inherited field partially replaced a Lua source group");
        host.update(0.25f);
        require(first.localTransform()[3].x == 3.5f && host.readExport(first.id(), "leaked") == PropertyValue(false),
                "Failed export validation retained new module entries or changed the old require() result");
        write(root, actor(directory));
        std::filesystem::last_write_time(root, rootStamp + std::chrono::seconds(4));
        require(host.reloadChanged() == 2 && host.lastReloadError().empty() &&
                    world.registry().get<SceneProcess>(independent.entity()).behavior == oldIndependent,
                "Lua inheritance/module recovery missed a user or touched an unrelated source");
        const auto count = std::get<int64_t>(host.readExport(second.id(), "sharedCount"));
        host.update(0.25f);
        require(first.localTransform()[3].x == 3 && second.localTransform()[3].x == 5 &&
                    std::get<int64_t>(host.readExport(second.id(), "sharedCount")) > count,
                "Successful helper replacement kept old code or reset an unchanged shared module");
        const auto cycle      = directory / "support/cycle_a.lua";
        const auto cycleStamp = std::filesystem::last_write_time(cycle);
        write(cycle, R"lua(
local value={revision=2};package.loaded['support.cycle_a']=value
value.other=require('support.cycle_b');return value
)lua");
        std::filesystem::last_write_time(cycle, cycleStamp + std::chrono::seconds(2));
        require(host.reloadChanged() == 2 && host.lastReloadError().empty(),
                "A cyclic require graph was invalidated while its replacement was still loading");
        host.update(0.25f);
        // A new dependency must include existing reverse users, even when the helper is not itself attached.
        write(late, "return {value=2}\n");
        write(root, actor(directory, false, true));
        std::filesystem::last_write_time(root, rootStamp + std::chrono::seconds(6));
        require(host.reloadChanged() == 3 && host.lastReloadError().empty() &&
                    world.registry().get<SceneProcess>(independent.entity()).behavior != oldIndependent,
                "A new Lua helper dependency did not include its existing reverse users");
        host.update(0.25f);
        require(independent.localTransform()[3].x == 0.5f, "Expanded Lua helper users did not share the new module");
        std::filesystem::remove(helper);
        require(host.reloadChanged() == 0 && !host.lastReloadError().empty(), "Missing Lua helper was ignored");
        host.update(0.25f);
        require(first.localTransform()[3].x == 3, "Missing Lua helper damaged the loaded package cache");
        write(helper, "return {value=1}\n");
        std::filesystem::last_write_time(helper, helperStamp + std::chrono::seconds(8));
        require(host.reloadChanged() == 3 && host.lastReloadError().empty(), "Missing Lua helper did not recover");
        host.update(0.25f);
        require(first.localTransform()[3].x == 1.5f, "Restored Lua helpers did not reapply initial exports");
        host.stop();
    }

    void attachmentFailures(const std::filesystem::path& directory)
    {
        std::filesystem::create_directories(directory);
        const auto helper  = directory / "value.lua";
        const auto root    = directory / "probe.lua";
        const auto invalid = directory / "invalid.lua";
        write(helper, "return {value=1}\n");
        const auto prelude = pathPrelude(directory) + "local value=require('value')\n";
        write(invalid, prelude + "local A={};local B={extends=A};A.extends=B;return A\n");
        SceneTree  world(std::make_unique<Node>("Lua attachment transactions"));
        auto&      first = world.addChild(world.root(), std::make_unique<Node3D>("First"));
        auto&      spare = world.addChild(world.root(), std::make_unique<Node3D>("Spare"));
        ScriptHost host(world, true);
        const auto attach = [&](const Node& node, const std::filesystem::path& path)
        {
            host.add({ScriptLanguage::eLua, path, {}, node.idInScene().value});
        };
        const auto rejects = [&](const Node& node, const std::filesystem::path& path, std::string_view cause)
        {
            bool rejected = false;
            try
            {
                attach(node, path);
            }
            catch (const std::exception& error)
            {
                rejected = std::string_view(error.what()).contains(cause);
            }
            require(rejected && !world.registry().get<SceneProcess>(node.entity()).behavior,
                    "Invalid Lua attachment did not fail atomically with its cause");
        };
        rejects(first, invalid, "inheritance contains a cycle");
        write(helper, "return {value=2}\n");
        write(root, pathPrelude(directory) + R"lua(
local value,source=require('value');assert(type(source)=='string')
package.preload.test_preload=function() return 42 end
assert(require('test_preload')==42)
assert(select('#',require('table'))==1)
return {class_name='Probe',extends='Node3D',exports={signal={kind='int',default=value.value}}}
)lua");
        attach(first, root);
        require(host.readExport(first.id(), "signal") == PropertyValue(int64_t {2}),
                "A failed initial attachment retained a new require cache entry");
        write(invalid, prelude + "return {extends=42}\n");
        rejects(spare, invalid, "extends requires");
        write(helper, "return {value=3}\n");
        rejects(spare, root, "Reload dependent Lua scripts");
        require(host.readExport(first.id(), "signal") == PropertyValue(int64_t {2}),
                "Attaching a changed live module altered an existing script");
        auto detached = world.remove(first);
        attach(spare, root);
        require(host.readExport(spare.id(), "signal") == PropertyValue(int64_t {3}),
                "A cached module without live users did not refresh for a new attachment");
        host.stop();
    }
} // namespace

int main()
try
{
    const auto directory = std::filesystem::path("build/.tmp") / ("lua-modules-" + StableId::generate().toString());
    inheritanceAndModules(directory);
    attachmentFailures(directory / "attachments");
    std::cout << "Lua inheritance/modules passed: inherited callbacks/exports, declaring origins, transitive/cyclic "
                 "require, cache rollback, source-group expansion, missing-helper recovery and attachment failures\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
