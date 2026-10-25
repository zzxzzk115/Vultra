#include <vultra/assets/asset_source.hpp>
#include <vultra/assets/vpk_export.hpp>
#include <vultra/scene/node_3d.hpp>
#include <vultra/scene/scene_tree.hpp>
#include <vultra/scripting/script_host.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace vultra;
    using Json = nlohmann::json;

    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    template<class Function>
    void rejects(Function&& function, std::string_view cause)
    {
        try
        {
            function();
        }
        catch (const std::exception& error)
        {
            require(std::string_view(error.what()).contains(cause), "Script configuration failed for the wrong cause");
            return;
        }
        throw std::runtime_error("Invalid authored script configuration was accepted");
    }

    void write(const std::filesystem::path& path, std::string_view text)
    {
        std::ofstream file(path);
        file << text;
        require(bool(file), "Write scene script fixture");
    }

    std::filesystem::path library(const std::filesystem::path& binaries, const char* name)
    {
#ifdef _WIN32
        return binaries / (std::string(name) + ".dll");
#else
        return binaries / ("lib" + std::string(name) + ".so");
#endif
    }

    void run(ScriptLanguage language, const std::filesystem::path& binaries, const std::filesystem::path& directory)
    {
        const bool        managed = language == ScriptLanguage::eCSharp;
        const bool        lua     = language == ScriptLanguage::eLua;
        const auto        energy  = managed ? "Energy" : "energy";
        const auto        partner = managed ? "Partner" : "partner";
        const auto        peers   = managed ? "Peers" : "peers";
        const auto        ready   = managed ? "ReadyEnergy" : "readyEnergy";
        const auto        last    = managed ? "LastEnergy" : "lastEnergy";
        const std::string prefix  = managed ? "VultraTests." : "";
        std::filesystem::create_directories(directory / "code");
        ProjectManifest project;
        project.mainScene = "main.vscene";
        std::filesystem::path beaconPath;
        std::filesystem::path pilotPath;
        std::filesystem::path replacement;
        const auto            beaconSource = [](int revision)
        {
            return "local settings=require('settings')\n"
                   "return {class_name='ReferenceBeacon',extends='Node3D',exports={"
                   "energy={kind='int',integer_bits=32,default=settings.energy},"
                   "revision={kind='int',integer_bits=32,default=" +
                   std::to_string(revision) + "}}}\n";
        };
        if (lua)
        {
            beaconPath = "code/beacon.lua";
            pilotPath  = "code/pilot.lua";
            write(directory / "settings.lua", "return {energy=7}\n");
            project.addAsset("settings.lua");
            write(directory / beaconPath, beaconSource(1));
            write(directory / pilotPath, R"lua(
local Script={class_name='ReferencePilot',extends='Node3D',exports={
    partner={kind='reference',reference={category='node',type='Node3D',nullable=false,
        class={module='beacon.lua',name='ReferenceBeacon'}}},
    peers={kind='array',element_kind='reference',default={},reference={category='node',type='Node3D',
        class={module='beacon.lua',name='ReferenceBeacon'}}},
    readyEnergy={kind='int',integer_bits=32,default=0},lastEnergy={kind='int',integer_bits=32,default=0}
}}
function Script:_ready() self.readyEnergy=self.partner:get_property('energy') end
function Script:_process(delta)
    self.lastEnergy=self.partner:get_property('energy')*self.partner:get_property('revision')
    self.node:set_position(self.lastEnergy*delta,#self.peers,0)
end
return Script
)lua");
        }
        else
        {
            const auto source = managed ?
                                    std::filesystem::path("build/.tmp/scripting-managed/Vultra.SceneScripts.dll") :
                                    library(binaries, "test-native-reference-script");
            replacement =
                managed ?
                    std::filesystem::path("build/.tmp/scripting-managed-identity-replacement/Vultra.SceneScripts.dll") :
                    library(binaries, "test-native-reference-script-replacement");
            beaconPath = std::filesystem::path("code") / source.filename();
            pilotPath  = beaconPath;
            if (managed)
            {
                for (const auto& file : std::filesystem::directory_iterator(source.parent_path()))
                {
                    if (file.is_regular_file())
                    {
                        std::filesystem::copy_file(file.path(), directory / "code" / file.path().filename());
                    }
                }
            }
            else
            {
                std::filesystem::copy_file(source, directory / beaconPath);
            }
        }
        const auto beaconModule = project.addAsset(beaconPath);
        const auto pilotModule  = lua ? project.addAsset(pilotPath) : beaconModule;
        project.scripts.push_back({beaconModule, language});
        if (lua)
        {
            project.scripts.push_back({pilotModule, language});
        }
        SceneTree  world(std::make_unique<Node>("Script authoring"));
        auto&      pilot   = world.addChild(world.root(), std::make_unique<Node3D>("Pilot"));
        auto&      first   = world.addChild(world.root(), std::make_unique<Node3D>("First beacon"));
        auto&      second  = world.addChild(world.root(), std::make_unique<Node3D>("Second beacon"));
        const auto firstId = first.idInScene();
        const auto pilotId = pilot.idInScene();
        const Json pilotValues {{partner, firstId.value.toString()},
                                {peers, Json::array({second.idInScene().value.toString(), nullptr})}};
        pilot.setScript(SceneScript {pilotModule, prefix + "ReferencePilot", pilotValues.dump()});
        first.setScript(SceneScript {beaconModule, prefix + "ReferenceBeacon", Json({{energy, 11}}).dump()});
        second.setScript(SceneScript {beaconModule, prefix + "ReferenceBeacon", Json({{energy, 13}}).dump()});
        const auto authored = world.serialize();
        auto       badShape = *first.script();
        badShape.exports    = "[]";
        rejects(
            [&]
            {
                first.setScript(badShape);
            },
            "JSON object");
        badShape = *first.script();
        badShape.className.clear();
        rejects(
            [&]
            {
                first.setScript(badShape);
            },
            "class name");
        auto invalid    = *second.script();
        invalid.exports = "{\"unknown_field\":1}";
        second.setScript(invalid);
        AssetSource files(directory);
        ScriptHost  host(world, true, &project);
        rejects(
            [&]
            {
                host.addScene(files);
            },
            "unknown_field");
        for (const auto* node : {&pilot, &first, &second})
        {
            require(!world.registry().get<SceneProcess>(node->entity()).behavior,
                    "Failed authored exports partially published a script batch");
        }
        invalid.exports = Json({{energy, 13}}).dump();
        second.setScript(invalid);
        if (language == ScriptLanguage::eNative)
        {
            const auto path  = directory / beaconPath;
            const auto stamp = std::filesystem::last_write_time(path);
            std::filesystem::last_write_time(path, stamp + std::chrono::seconds(2));
            rejects(
                [&]
                {
                    host.addScene(files);
                },
                "Reload native modules before attaching a changed source");
            require(host.reloadChanged() == 1, "Failed attachment retained an unreloadable native provider");
        }
        auto& wrongBase = world.addChild(world.root(), std::make_unique<Node>("Wrong native base"));
        wrongBase.setScript(SceneScript {beaconModule, prefix + "ReferenceBeacon"});
        // Managed code validates the native base before exposing its property API.
        rejects(
            [&]
            {
                host.addScene(files);
            },
            managed ? "Initialize plugin failed" : "class/base does not match");
        wrongBase.setScript(std::nullopt);
        world.remove(wrongBase);
        host.addScene(files);
        host.update(0.25f);
        require(host.readExport(pilot.id(), ready) == PropertyValue(int64_t {11}) &&
                    pilot.localTransform()[3] == glm::vec4(2.75f, 2, 0, 1),
                "Forward references or initial authored exports were unavailable in Ready/process");
        host.writeExport(first.id(), energy, int64_t {17});
        require(host.readExport(second.id(), energy) == PropertyValue(int64_t {13}),
                "Two authored instances share export storage");
        rejects(
            [&]
            {
                host.captureExports(first.id());
            },
            "stopped world");
        world.stop();
        host.captureExports(first.id());
        auto changedClass      = *first.script();
        changedClass.className = "OtherClass";
        rejects(
            [&]
            {
                first.setScript(changedClass);
            },
            "Detach the behavior");
        host.writeExport(first.id(), energy, int64_t {23});
        require(Json::parse(first.script()->exports).at(energy) == 17,
                "An ordinary runtime edit rewrote authored initial configuration");
        const auto validInitial   = *first.script();
        auto       invalidInitial = validInitial;
        invalidInitial.exports    = Json({{energy, "invalid integer"}}).dump();
        first.setScript(invalidInitial);
        const auto stamp = std::filesystem::last_write_time(directory / beaconPath);
        if (lua)
        {
            write(directory / beaconPath, beaconSource(2));
        }
        else
        {
            std::filesystem::copy_file(replacement,
                                       directory / beaconPath,
                                       std::filesystem::copy_options::overwrite_existing);
        }
        std::filesystem::last_write_time(directory / beaconPath, stamp + std::chrono::seconds(2));
        require(host.reloadChanged() == 0 && !host.lastReloadError().empty() &&
                    host.readExport(first.id(), energy) == PropertyValue(int64_t {23}),
                "Invalid authored configuration partially replaced live code or values");
        first.setScript(validInitial);
        require(host.reloadChanged(true) == (language == ScriptLanguage::eNative ? 1 : 3) &&
                    host.lastReloadError().empty() &&
                    host.readExport(first.id(), energy) == PropertyValue(int64_t {17}),
                "Replacement did not use the current scene-owned initial exports");
        host.update(0.25f);
        require(host.readExport(pilot.id(), last) == PropertyValue(int64_t {34}),
                "Replaced authored scripts did not share current types and code");
        host.stop();
        rejects(
            [&]
            {
                host.addScene(files);
            },
            "stopped host");
        rejects(
            [&]
            {
                host.captureExports(first.id());
            },
            "stopped host");
        auto detached = world.remove(second);
        require(detached->script() && detached->script()->module == beaconModule,
                "Detaching a node lost authored script configuration");
        world.addChild(world.root(), std::move(detached));
        std::filesystem::rename(directory / "code", directory / "renamed");
        for (const auto id :
             lua ? std::vector<AssetId> {beaconModule, pilotModule} : std::vector<AssetId> {beaconModule})
        {
            project.renameAsset(id, std::filesystem::path("renamed") / project.asset(id).path.filename());
        }
        project.save(directory / "project.vproject");
        auto duplicateModule = project;
        duplicateModule.scripts.push_back(duplicateModule.scripts.front());
        rejects(
            [&]
            {
                duplicateModule.save(directory / "invalid.vproject");
            },
            "Duplicate project script");
        world.save(directory / project.mainScene);
        const auto saved  = world.serialize();
        auto       parsed = SceneTree::parse(saved);
        require(parsed.serialize() == saved && parsed.find(firstId)->id() != first.id() &&
                    parsed.find(firstId)->script()->module == beaconModule,
                "Scene-owned script configuration or persistent identity did not round-trip");
        auto original = SceneTree::parse(authored);
        require(Json::parse(original.find(firstId)->script()->exports).at(energy) == 11,
                "A saved authoring snapshot aliased live configuration");
        const auto archivePath = directory / "project.vpk";
        packProject(directory / "project.vproject", archivePath);
        for (bool packed : {false, true})
        {
            auto       source   = packed ? std::make_unique<AssetSource>(VpkArchive(archivePath)) :
                                           std::make_unique<AssetSource>(directory);
            const auto manifest = ProjectManifest::load(source->root() / "project.vproject", source.get());
            auto       restored = SceneTree::load(source->root() / manifest.mainScene, source.get());
            ScriptHost player(restored, false, &manifest);
            player.addScene(*source);
            player.update(0.25f);
            require(player.readExport(restored.find(firstId)->id(), energy) == PropertyValue(int64_t {17}) &&
                        player.readExport(restored.find(pilotId)->id(), last) == PropertyValue(int64_t {34}),
                    "Renamed module assets, saved references or filesystem/VPK script execution differ");
            player.stop();
        }
    }

    void rootModule(const std::filesystem::path& binaries, const std::filesystem::path& directory)
    {
        std::filesystem::create_directories(directory);
        const auto path = library(binaries, "test-native-root-script");
        std::filesystem::copy_file(path, directory / path.filename());
        ProjectManifest project;
        project.mainScene = "main.vscene";
        project.scripts.push_back({project.addAsset(path.filename()), ScriptLanguage::eNative});
        AssetSource source(directory);
        SceneTree   world(std::make_unique<Node3D>("Root callbacks"));
        ScriptHost  host(world, false, &project);
        host.addScene(source);
        host.addScene(source);
        host.update(0.25f);
        host.update(0.25f);
        require(static_cast<Node3D&>(world.root()).localTransform()[3].x == 0.5f,
                "A native module without classes lost its root callbacks or installed twice");
        host.stop();
    }
} // namespace

int main(int argc, char** argv)
try
{
    require(argc > 0, "Missing executable path");
    const auto root = std::filesystem::absolute("build/.tmp/scene-scripts-" + StableId::generate().toString());
    rootModule(std::filesystem::absolute(argv[0]).parent_path(), root / "root");
    for (const auto language : {ScriptLanguage::eNative, ScriptLanguage::eLua, ScriptLanguage::eCSharp})
    {
        run(language, std::filesystem::absolute(argv[0]).parent_path(), root / std::to_string(int(language)));
    }
    std::cout << "Scene scripts passed: three-language independent exports, typed forward references, failed/recovered "
                 "attachment, explicit capture, replacement, detach, module rename and filesystem/VPK execution\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
