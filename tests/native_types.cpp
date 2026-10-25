#include <vultra/api/native_extension.hpp>
#include <vultra/api/scene_bridge.hpp>
#include <vultra/scene/render_nodes.hpp>
#include <vultra/scene/scene_api.hpp>
#include <vultra/scene/scene_tree.hpp>
#ifdef TEST_NATIVE_TYPES_SCRIPTING
#include <vultra/scripting/script_host.hpp>
#endif

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

extern "C" VultraStatus vultraTestStaticExtensionInit(const VultraHostApi* host, VultraPluginApi* plugin);

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

    template<class Operation>
    void reject(Operation operation, std::string_view cause)
    {
        try
        {
            operation();
        }
        catch (const std::exception& error)
        {
            require(std::string_view(error.what()).find(cause) != std::string_view::npos,
                    "Native extension failed for an unexpected reason");
            return;
        }
        throw std::runtime_error("Invalid native extension operation succeeded");
    }

    std::filesystem::path library(const std::filesystem::path& binaries, const char* name)
    {
#ifdef _WIN32
        return binaries / (std::string(name) + ".dll");
#else
        return binaries / ("lib" + std::string(name) + ".so");
#endif
    }

    void staticExtension()
    {
        SceneTypeCatalog types;
        const auto       initialTypes = types.objects().types().size();
        reject(
            [&]
            {
                NativeExtension invalid(static_cast<VultraPluginInit>(nullptr), types);
            },
            "entry is missing");
        NativeExtension extension(vultraTestStaticExtensionInit, types);
        {
            SceneTree  world(std::make_unique<Node>("Static world"), types);
            const auto resource = sceneCreateResource(world, "CResource", "{}");
            require(world.findResource(resource) && world.objectType(resource).isA("Resource"),
                    "Static C extension did not register and construct its resource");
            reject(
                [&]
                {
                    extension.unload();
                },
                "copied catalogs");
        }
        extension.unload();
        require(types.objects().types().size() == initialTypes, "Static extension left registered types behind");
    }
} // namespace

int main(int argc, char** argv)
{
    staticExtension();
    require(argc > 0, "Missing executable path");
    const auto       binaries = std::filesystem::absolute(argv[0]).parent_path();
    SceneTypeCatalog types;
    const auto       initialTypes = types.objects().types().size();
    reject(
        [&]
        {
            NativeExtension invalid(library(binaries, "test-native-types-invalid"), types);
        },
        "Initialize plugin failed with API status");
    require(types.objects().types().size() == initialTypes, "Failed initialization left staged types registered");
    NativeExtension extension(library(binaries, "test-native-types-code"), types);
    NativeExtension cExtension(library(binaries, "test-native-types-c"), types);
    {
        NativeExtension derived(library(binaries, "test-native-types-derived"), types);
        reject(
            [&]
            {
                extension.unload();
            },
            "Another extension derives");
    }
    require(types.objects().type("FastMover").isA("Node3D") && types.objects().type("TestSettings").isA("Resource"),
            "Native registered type ancestry is incomplete");
    reject(
        [&]
        {
            NativeExtension duplicate(library(binaries, "test-native-types-code"), types);
        },
        "Initialize plugin failed with API status");
    {
        auto copy  = types;
        auto moved = std::move(copy);
        reject(
            [&]
            {
                extension.unload();
            },
            "copied catalogs");
        copy  = moved;
        moved = std::move(copy);
        require(moved.objects().type("FastMover").isA("TestMover"), "Catalog move lost type records");
    }
    std::unique_ptr<Node> detached;
    {
        SceneTree  world(std::make_unique<Node>("World"), types);
        SceneTree  foreign(std::make_unique<Node>("Foreign"));
        const auto first     = sceneCreateNodeType(world, nullptr, world.root().id(), "TestMover", "First", "{}");
        const auto second    = sceneCreateNodeType(world, nullptr, world.root().id(), "FastMover", "Second", "{}");
        const auto tuning    = sceneCreateResource(world, "TestSettings", "{}");
        const auto cResource = sceneCreateResource(world, "CResource", "{}");
        require(world.findResource(cResource) && world.objectType(cResource).isA("Resource"),
                "Pure C extension did not construct its real resource");
        const auto grouped = sceneCreateResource(world, "GroupedResource", "{\"extension\":{\"color\":[0.1,0.75]}}");
        require(world.readProperty(grouped, "red") == PropertyValue(0.1f) &&
                    world.readProperty(grouped, "green") == PropertyValue(0.75f),
                "Grouped extension fields lost their JSON paths or float normalization");
        reject(
            [&]
            {
                sceneCreateResource(world, "GroupedResource", "{\"extension\":{\"color\":[0.1,0.75,1]}}");
            },
            "unknown extension array");
        world.writeProperty(tuning, "drag", 3.0f);
        world.writeProperty(tuning, "target", PropertyReference {world.find(first)->idInScene().value});
        world.writeProperty(first, "speed", 2.0f);
        require(world.readProperty(second, "speed") == PropertyValue(1.0f), "Two native instances share mutable state");
        world.addComponent(first, "TestVelocity", "{\"value\":3}");
        world.writeComponentProperty(first,
                                     "TestVelocity",
                                     "settings",
                                     PropertyReference {world.findResource(tuning)->resourceId().value});
        require(world.readComponentProperty(first, "TestVelocity", "value") == PropertyValue(3.0f),
                "Extension component storage differs from property reads");
        const auto revision = world.changes().metadata;
        reject(
            [&]
            {
                world.writeComponentProperty(first, "TestVelocity", "value", -1.0f);
            },
            "write value");
        require(world.changes().metadata == revision &&
                    world.readComponentProperty(first, "TestVelocity", "value") == PropertyValue(3.0f),
                "Failed semantic write changed component state or published notifications");
        world.writeComponentProperty(first, "TestVelocity", "value", 4.0f);
        const auto bare = world.createEntity("Bare ECS");
        sceneAddComponent(world, bare, "TestVelocity", "{}");
        require(!world.findObject(bare) &&
                    world.readComponentProperty(bare, "TestVelocity", "value") == PropertyValue(1.0f),
                "Native component construction requires a Node facade");
        reject(
            [&]
            {
                world.addComponent(bare, "TestVelocity", "{}");
            },
            "absent component");
        world.removeComponent(bare, "TestVelocity");
        world.destroyEntity(bare);
        reject(
            [&]
            {
                sceneCreateNodeType(world, nullptr, foreign.root().id(), "TestMover", "Bad", "{}");
            },
            "parent");
        reject(
            [&]
            {
                sceneCreateNodeType(world,
                                    nullptr,
                                    world.root().id(),
                                    "TestMover",
                                    "Bad",
                                    "{\"extension\":{\"typo\":1}}");
            },
            "unknown");
        reject(
            [&]
            {
                sceneCreateNodeType(world,
                                    nullptr,
                                    world.root().id(),
                                    "TestMover",
                                    "Bad",
                                    "{\"extension\":{\"speed\":-1}}");
            },
            "create extension instance");
        world.start();
        world.advance(0.5);
        require(static_cast<Node3D*>(world.find(first))->localTransform()[3].x == 1 &&
                    static_cast<Node3D*>(world.find(second))->localTransform()[3].x == 1 &&
                    world.readProperty(first, "ticks") == PropertyValue(int64_t {1}),
                "Native lifecycle logic did not execute against the real scene transform");
        world.stop();

        Camera3D camera("Base camera");
        auto     settings    = camera.settings();
        settings.verticalFov = 0.85f;
        settings.nearPlane   = 0.025f;
        camera.setSettings(settings);
        auto cameraState         = Json::parse(types.serialize(camera));
        cameraState["extension"] = {{"aperture", 4}};
        const auto cameraId =
            sceneCreateNodeType(world, nullptr, world.root().id(), "TestCamera", "Camera", cameraState.dump());
        sceneSetCurrentCamera(world, cameraId);
        const auto* extendedCamera = dynamic_cast<Camera3D*>(world.find(cameraId));
        require(extendedCamera && world.currentCamera() == cameraId && extendedCamera->settings() == settings,
                "Native camera extension lost its native render facade");
        Light3D light("Base light", RenderLightKind::eSpot);
        auto    lightSettings   = light.settings();
        lightSettings.intensity = 4.25f;
        lightSettings.range     = 12;
        light.setSettings(lightSettings);
        const auto lightId =
            sceneCreateNodeType(world, nullptr, world.root().id(), "TestLight", "Light", types.serialize(light));
        const auto* extendedLight = dynamic_cast<Light3D*>(world.find(lightId));
        require(extendedLight && extendedLight->lightKind() == RenderLightKind::eSpot &&
                    extendedLight->settings() == lightSettings,
                "Native light extension lost component settings");
        WorldEnvironment environment("Base environment");
        environment.setSettings({1.75f});
        const auto  environmentId       = sceneCreateNodeType(world,
                                                              nullptr,
                                                              world.root().id(),
                                                              "TestEnvironment",
                                                              "Environment",
                                                              types.serialize(environment));
        const auto* extendedEnvironment = dynamic_cast<WorldEnvironment*>(world.find(environmentId));
        require(extendedEnvironment && extendedEnvironment->settings() == environment.settings(),
                "Native environment extension lost component settings");
        MaterialResource material("Base material");
        auto             materialState = Json::parse(types.serialize(material));
        materialState["extension"]     = {{"absorption", 0.25}};
        const auto materialId          = sceneCreateResource(world, "TestMaterial", materialState.dump());
        require(world.findMaterial(materialId) && world.readProperty(materialId, "absorption") == PropertyValue(0.25f),
                "Native material extension lost resource/material indexing");
        MeshInstance3D mesh("Base mesh", {StableId::generate()});
        mesh.setMaterial(0, world.findResource(materialId)->resourceId());
        auto meshState         = Json::parse(types.serialize(mesh));
        meshState["extension"] = {{"lod", 1}};
        auto& extendedMesh =
            world.addChild(world.root(), types.create("TestMesh", "Mesh", {StableId::generate()}, meshState.dump()));
        const auto& typedMesh = dynamic_cast<const MeshInstance3D&>(extendedMesh);
        require(typedMesh.model() == mesh.model() && typedMesh.materialOverrides().size() == 1 &&
                    typedMesh.materialOverrides().front().slot == 0 &&
                    typedMesh.materialOverrides().front().material == world.findResource(materialId)->resourceId(),
                "Native mesh extension lost geometry identity or material overrides");
#ifdef TEST_NATIVE_TYPES_SCRIPTING
        {
            const auto actor = sceneCreateNodeType(world, nullptr, world.root().id(), "TestMover", "Provided", "{}");
            ScriptHost host(world);
            const auto provider = library(binaries, "test-native-script-classes");
            host.addNativeModule(provider);
            host.add({ScriptLanguage::eNative, provider, "First", world.find(actor)->idInScene().value});
            host.update(0.25f);
            require(static_cast<Node3D*>(world.find(actor))->localTransform()[3].x == 1 &&
                        host.scriptClass(actor).name == "First",
                    "Native type and native script-provider owners cannot coexist");
            host.stop();
            world.remove(*world.find(actor));
        }
        for (const auto language : {ScriptLanguage::eNative, ScriptLanguage::eLua, ScriptLanguage::eCSharp})
        {
            const bool managed = language == ScriptLanguage::eCSharp;
            const auto actor   = sceneCreateNodeType(world, nullptr, world.root().id(), "TestMover", "Scripted", "{}");
            ScriptHost host(world, false);
            const std::string type = managed ? "VultraTests.ValueProbe" : "ValueProbe";
            const auto        file =
                language == ScriptLanguage::eNative ?
                    library(binaries, "test-native-value-script") :
                    std::filesystem::path(managed ? "build/.tmp/scripting-managed/Vultra.SceneScripts.dll" :
                                                    "tests/scene_scripts/values.lua");
            const ScriptExport initial[] {
                {managed ? "Mode" : "mode", PropertyEnum {2}},
                {managed ? "Target" : "target", PropertyReference {world.find(first)->idInScene().value}},
                {managed ? "Material" : "material",
                 PropertyReference {world.findResource(materialId)->resourceId().value}}};
            host.add({language, file, type, world.find(actor)->idInScene().value, initial});
            host.update(0.125);
            const auto position = static_cast<Node3D*>(world.find(actor))->localTransform()[3];
            require(position.x == 4 && position.z == 4 &&
                        world.readProperty(actor, "ticks") == PropertyValue(int64_t {1}) &&
                        host.readExport(actor, managed ? "ReadyTarget" : "readyTarget") ==
                            PropertyValue(std::string("First")),
                    "Native extension and language script do not share real objects/references and lifecycle");
            host.stop();
            world.remove(*world.find(actor));
        }
#endif

        const auto saved = world.serialize();
        const auto snapshot =
            std::filesystem::path("build/.tmp") / ("native-types-" + StableId::generate().toString() + ".vscene");
        std::ofstream savedFile(snapshot);
        savedFile << saved;
        savedFile.close();
        require(bool(savedFile), "Write native scene regression snapshot");
        auto        loaded      = SceneTree::parse(saved, types);
        const auto* loadedFirst = loaded.find(world.find(first)->idInScene());
        require(loadedFirst && loaded.readProperty(loadedFirst->id(), "speed") == PropertyValue(2.0f) &&
                    loaded.readComponentProperty(loadedFirst->id(), "TestVelocity", "value") == PropertyValue(4.0f),
                "Native object/component fields did not survive save/reload");
        require(loaded.serialize() == saved, "Native scene persistence is not deterministic");
        SceneAccess            access {&world, 1, true};
        const VultraSceneFrame frame {&access, 1};
        const std::string      value = "6.0";
        require(sceneApi().write_component_property(frame,
                                                    first.value,
                                                    "TestVelocity",
                                                    12,
                                                    "value",
                                                    5,
                                                    value.data(),
                                                    value.size()) == VULTRA_STATUS_OK &&
                    world.readComponentProperty(first, "TestVelocity", "value") == PropertyValue(6.0f),
                "Generated component ABI does not reach native extension storage");
        require(sceneApi().add_component(frame, foreign.root().id().value, "TestVelocity", 12, "{}", 2) ==
                    VULTRA_STATUS_INVALID_ARGUMENT,
                "Generated component ABI accepted a foreign world identity");
        access.active = false;
        require(sceneApi().remove_component(frame, first.value, "TestVelocity", 12) == VULTRA_STATUS_INVALID_FRAME,
                "Native component ABI accepts an expired frame");
        detached       = world.remove(*world.find(first));
        auto& restored = world.addChild(world.root(), std::move(detached));
        require(restored.id() == first && world.readProperty(first, "speed") == PropertyValue(2.0f) &&
                    world.readComponentProperty(first, "TestVelocity", "value") == PropertyValue(6.0f),
                "Detach/reattach lost native instance or independent component state");
        // This subtree will move without ownership of its old world's resource.
        world.writeComponentProperty(first, "TestVelocity", "settings", PropertyReference {});
        detached = world.remove(restored);
        reject(
            [&]
            {
                extension.unload();
            },
            "copied catalogs");
    }
    require(types.nodeType("TestMover").lifetime->catalogs == 1 && types.nodeType("TestMover").lifetime->instances == 1,
            "World teardown leaked catalog borrows or lost the detached instance");
    reject(
        [&]
        {
            extension.unload();
        },
        "instances");
    SceneTree reopened(std::move(detached), types);
    require(reopened.root().typeName() == "TestMover" &&
                reopened.readComponentProperty(reopened.root().id(), "TestVelocity", "value") == PropertyValue(6.0f),
            "Detached native state was lost when constructed as another world's root");
    reopened = SceneTree(std::make_unique<Node>("Retired"));
    extension.unload();
    cExtension.unload();
    require(types.objects().types().size() == initialTypes, "Native unload left type registrations behind");
    std::cout << "Native types passed: registration, ECS storage, logic, state persistence, context checks and unload "
                 "ownership\n";
}
