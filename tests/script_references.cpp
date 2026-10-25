#include <vultra/scene/node_3d.hpp>
#include <vultra/scene/scene_tree.hpp>
#include <vultra/scripting/script_host.hpp>

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

    std::filesystem::path library(const std::filesystem::path& binaries, const char* name)
    {
#ifdef _WIN32
        return binaries / (std::string(name) + ".dll");
#else
        return binaries / ("lib" + std::string(name) + ".so");
#endif
    }

    void writeFile(const std::filesystem::path& path, std::string_view text)
    {
        std::ofstream file(path);
        file << text;
        file.close();
        require(bool(file), "Write script reference fixture");
    }

    void writeBeacon(const std::filesystem::path& path, int revision, bool bad = false)
    {
        writeFile(path,
                  "return {class_name='" + std::string(bad ? "ReferenceIntruder" : "ReferenceBeacon") +
                      "',extends='Node3D',exports={energy={kind='int',integer_bits=32,default=7},"
                      "revision={kind='int',integer_bits=32,default=" +
                      std::to_string(revision) + "}}}");
    }

    void
    references(ScriptLanguage language, const std::filesystem::path& binaries, const std::filesystem::path& directory)
    {
        const bool managed = language == ScriptLanguage::eCSharp;
        const bool native  = language == ScriptLanguage::eNative;
        const auto partner = managed ? "Partner" : "partner";
        const auto peers   = managed ? "Peers" : "peers";
        const auto energy  = managed ? "Energy" : "energy";
        const auto ready   = managed ? "ReadyEnergy" : "readyEnergy";
        const auto last    = managed ? "LastEnergy" : "lastEnergy";
        const auto prefix  = managed ? "VultraTests." : "";
        std::filesystem::create_directories(directory);
        std::filesystem::path active;
        std::filesystem::path bad;
        std::filesystem::path good;
        std::filesystem::path pilot;
        std::filesystem::path alien;
        if (native || managed)
        {
            const auto source = native ? library(binaries, "test-native-reference-script") :
                                         std::filesystem::path("build/.tmp/scripting-managed/Vultra.SceneScripts.dll");
            bad = native ? library(binaries, "test-native-reference-script-bad") :
                           std::filesystem::path("build/.tmp/scripting-managed-narrow/Vultra.SceneScripts.dll");
            good =
                native ?
                    library(binaries, "test-native-reference-script-replacement") :
                    std::filesystem::path("build/.tmp/scripting-managed-identity-replacement/Vultra.SceneScripts.dll");
            if (managed)
            {
                for (const auto& file : std::filesystem::directory_iterator(source.parent_path()))
                {
                    if (file.is_regular_file())
                    {
                        std::filesystem::copy_file(file.path(), directory / file.path().filename());
                    }
                }
            }
            active = directory / ("active" + source.extension().string());
            std::filesystem::copy_file(source, active);
            pilot = active;
            alien = active;
        }
        else
        {
            active = directory / "beacon.lua";
            pilot  = directory / "pilot.lua";
            alien  = directory / "alien.lua";
            bad    = directory / "bad.lua";
            good   = directory / "good.lua";
            writeBeacon(active, 1);
            writeBeacon(bad, 2, true);
            writeBeacon(good, 2);
            writeFile(alien, "return {class_name='ReferenceAlien',extends='Node3D'}");
            writeFile(pilot, R"(
local Script = {class_name='ReferencePilot',extends='Node3D',exports={
    partner={kind='reference',reference={category='node',type='Node3D',nullable=false,
        class={module='beacon.lua',name='ReferenceBeacon'}}},
    peers={kind='array',element_kind='reference',default={},reference={category='node',type='Node3D',
        class={module='beacon.lua',name='ReferenceBeacon'}}},
    readyEnergy={kind='int',integer_bits=32,default=0},lastEnergy={kind='int',integer_bits=32,default=0}
}}
function Script:energy()
    return self.partner:get_property('energy') * self.partner:get_property('revision')
end
function Script:_ready() self.readyEnergy = self:energy() end
function Script:_process(delta)
    self.lastEnergy = self:energy()
    self.node:set_position(self.lastEnergy*delta,#self.peers,0)
end
return Script
)");
        }
        SceneTree  world(std::make_unique<Node>("Script references"));
        const auto makeNode = [&](const char* name) -> Node3D&
        {
            return static_cast<Node3D&>(world.addChild(world.root(), std::make_unique<Node3D>(name)));
        };
        auto&      owner  = makeNode("Pilot");
        auto&      first  = makeNode("First beacon");
        auto&      second = makeNode("Second beacon");
        auto&      wrong  = makeNode("Alien");
        auto&      plain  = makeNode("No script");
        ScriptHost host(world, true);
        if (native)
        {
            host.addNativeModule(active);
        }
        const auto module = [&](const char* type, const Node& node)
        {
            return ScriptAttachment {language, {}, std::string(prefix) + type, node.idInScene().value};
        };
        const std::array firstValues {ScriptExport {energy, int64_t {11}}};
        const std::array secondValues {ScriptExport {energy, int64_t {13}}};
        std::array       ownerValues {ScriptExport {partner, PropertyReference {wrong.idInScene().value}},
                                      ScriptExport {peers,
                                                    PropertyArray {{PropertyReference {first.idInScene().value},
                                                                    PropertyReference {},
                                                                    PropertyReference {second.idInScene().value}}}}};
        auto             beaconModule = module("ReferenceBeacon", first);
        // Lua declares the class in its source so a renamed candidate reaches prospective reference validation.
        if (!native && !managed)
        {
            beaconModule.typeName.clear();
        }
        auto secondModule = beaconModule;
        secondModule.node = second.idInScene().value;
        if (managed)
        {
            secondModule.typeName = "VultraTests.ReferenceSpecialBeacon";
        }
        auto ownerModule     = module("ReferencePilot", owner);
        ownerModule.path     = pilot;
        ownerModule.exports  = ownerValues;
        beaconModule.path    = active;
        beaconModule.exports = firstValues;
        secondModule.path    = active;
        secondModule.exports = secondValues;
        auto alienModule     = module("ReferenceAlien", wrong);
        alienModule.path     = alien;
        const std::array attachments {ownerModule, beaconModule, secondModule, alienModule};
        bool             rejected = false;
        try
        {
            host.add(attachments);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(rejected && !world.registry().get<SceneProcess>(owner.entity()).behavior &&
                    !world.registry().get<SceneProcess>(first.entity()).behavior,
                "An invalid forward reference published part of the initial attachment batch");
        ownerValues[0].value = PropertyReference {first.idInScene().value};
        host.add(attachments);
        const auto& schema = host.scriptClass(owner.id()).property(partner);
        require(schema.referenceType == "Node3D" && schema.referenceClass &&
                    schema.referenceClass->module == std::filesystem::canonical(active).generic_string() &&
                    schema.referenceClass->name == std::string(prefix) + "ReferenceBeacon",
                "Class metadata lost its native base or exposed a staged module origin");
        require(world.behaviorClasses(second.id()).size() == (managed ? 2U : 1U),
                "User script ancestry was not published");
        host.update(0.25f);
        require(host.readExport(owner.id(), ready) == PropertyValue(int64_t {11}) &&
                    owner.localTransform()[3].x == 2.75f && owner.localTransform()[3].y == 3,
                "Ready/process did not resolve forward-configured script fields and reference arrays");
        for (const auto* invalid : {&wrong, &plain})
        {
            rejected = false;
            try
            {
                world.writeProperty(owner.id(), partner, PropertyReference {invalid->idInScene().value});
            }
            catch (const std::invalid_argument&)
            {
                rejected = true;
            }
            require(rejected && host.readExport(owner.id(), partner) == ownerValues[0].value,
                    "An invalid script-class assignment mutated the live reference");
        }
        const auto* oldOwner = world.registry().get<SceneProcess>(owner.entity()).behavior;
        const auto* oldFirst = world.registry().get<SceneProcess>(first.entity()).behavior;
        const auto* oldAlien = world.registry().get<SceneProcess>(wrong.entity()).behavior;
        host.writeExport(first.id(), energy, int64_t {17});
        const auto stamp   = std::filesystem::last_write_time(active);
        const auto replace = [&](const std::filesystem::path& candidate, int seconds)
        {
            std::filesystem::copy_file(candidate, active, std::filesystem::copy_options::overwrite_existing);
            std::filesystem::last_write_time(active, stamp + std::chrono::seconds(seconds));
        };
        replace(bad, 2);
        require(host.reloadChanged() == 0 && !host.lastReloadError().empty() &&
                    world.registry().get<SceneProcess>(owner.entity()).behavior == oldOwner &&
                    world.registry().get<SceneProcess>(first.entity()).behavior == oldFirst &&
                    host.readExport(first.id(), energy) == PropertyValue(int64_t {17}),
                "Failed class/reference restoration partially replaced a dependency group");
        require(host.reloadChanged() == 0 && host.reloadChanged(true) == 0,
                "A failed class dependency group was partially committed on retry");
        host.update(0.25f);
        require(owner.localTransform()[3].x == 4.25f, "Failed replacement broke live typed script behavior");
        replace(good, 4);
        size_t count = 3;
        if (native)
        {
            count = 1;
        }
        else if (managed)
        {
            count = 4;
        }
        require(host.reloadChanged() == count && host.lastReloadError().empty(),
                "Dependency replacement did not recover every connected instance");
        if (!native && !managed)
        {
            require(world.registry().get<SceneProcess>(wrong.entity()).behavior == oldAlien,
                    "Cross-file dependency replacement reloaded an unrelated Lua source");
        }
        host.update(0.25f);
        require(owner.localTransform()[3].x == 5.5f && host.readExport(owner.id(), last) == PropertyValue(int64_t {22}),
                "Recovered typed references kept old code or lost their configured values");
        const auto* recoveredOwner = world.registry().get<SceneProcess>(owner.entity()).behavior;
        std::filesystem::remove(active);
        require(host.reloadChanged() == 0 && !host.lastReloadError().empty() && host.reloadChanged() == 0 &&
                    world.registry().get<SceneProcess>(owner.entity()).behavior == recoveredOwner,
                "A missing dependency discarded the active script group");
        replace(good, 6);
        require(host.reloadChanged() == count && host.lastReloadError().empty(),
                "Restoring a missing dependency did not recover the group");
        host.update(0.25f);
        require(owner.localTransform()[3].x == 5.5f, "Restored dependency changed typed script execution");
        host.stop();
    }
} // namespace

int main(int argc, char** argv)
try
{
    require(argc > 0, "Missing executable path");
    const auto binaries = std::filesystem::absolute(argv[0]).parent_path();
    const auto directory =
        std::filesystem::path("build/.tmp") / ("script-references-" + StableId::generate().toString());
    for (const auto language : {ScriptLanguage::eNative, ScriptLanguage::eLua, ScriptLanguage::eCSharp})
    {
        references(language, binaries, directory / std::to_string(int(language)));
    }
    std::cout << "Script references passed: typed forward batches, ancestry, dependency groups, failed replacement, "
                 "retry and missing-source recovery across Native/Lua/C#\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
