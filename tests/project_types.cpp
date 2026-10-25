#ifdef TEST_PROJECT_TYPES_RENDERING
#include "../editor/src/research_workspace.hpp"
#include "../examples/research/color_gain.hpp"
#endif

#include <vultra/assets/asset_source.hpp>
#include <vultra/assets/project_manifest.hpp>
#include <vultra/main/project_types.hpp>
#include <vultra/scene/render_nodes.hpp>
#include <vultra/scene/scene_api.hpp>
#include <vultra/scene/scene_tree.hpp>
#ifdef TEST_PROJECT_TYPES_RENDERING
#include <vultra/drivers/rhi/image_readback.hpp>
#endif

#include <glm/ext/matrix_transform.hpp>
#ifdef TEST_PROJECT_TYPES_RENDERING
#include <nlohmann/json.hpp>
#endif

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
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

    template<class Operation>
    void reject(Operation operation, std::string_view cause)
    {
        try
        {
            operation();
        }
        catch (const std::exception& error)
        {
            require(std::string_view(error.what()).contains(cause), "Project type failure has an unexpected cause");
            return;
        }
        throw std::runtime_error("Invalid project type operation succeeded");
    }

    std::filesystem::path library(const std::filesystem::path& directory, const char* name)
    {
#ifdef _WIN32
        return directory / (std::string(name) + ".dll");
#else
        return directory / ("lib" + std::string(name) + ".so");
#endif
    }

    void verifyWorld(SceneTree& world, NodeId moverId, ResourceId settingsId)
    {
        auto* mover = world.find(moverId);
        require(world.findResource(settingsId), "Project resource did not survive loading");
        const auto settings = world.findResource(settingsId)->id();
        require(mover && world.objectType(mover->id()).isA("TestMover") &&
                    world.readProperty(mover->id(), "speed") == PropertyValue(4.0f) &&
                    world.readProperty(settings, "drag") == PropertyValue(3.0f) &&
                    world.readProperty(settings, "target") == PropertyValue(PropertyReference {moverId.value}) &&
                    world.readComponentProperty(mover->id(), "TestVelocity", "settings") ==
                        PropertyValue(PropertyReference {settingsId.value}),
                "Project types or serialized object/component references did not survive loading");
        auto detached = world.remove(*mover);
        require(!world.find(moverId), "Detached extension Node retained its world identity");
        auto& attached = world.addChild(world.root(), std::move(detached));
        world.start();
        world.advance(0.25);
        world.stop();
        require(static_cast<Node3D&>(attached).localTransform()[3].x == 1 &&
                    world.readProperty(attached.id(), "ticks") == PropertyValue(int64_t {1}),
                "Project type code did not survive loading, detach and execution");
    }

#ifdef TEST_PROJECT_TYPES_RENDERING
    void verifyWorkspace(const std::filesystem::path& directory)
    {
        Device          device;
        RenderingServer server(device);
        PassCatalog     catalog(device);
        catalog.add(research::colorGainDefinition());
        ResearchWorkspace workspace(device, server, catalog);
        ResearchDocument  document;
        document.project    = directory / "project.vproject";
        document.size       = {129, 97};
        document.definition = {{{"gain", "research.color_gain", {{"gain", 1}}}},
                               {{"scene.hdr", "gain.source"}},
                               {"gain.color", "scene.hdr"}};
        workspace.replace(document);
        Frame      frame(device);
        const auto render = [&]
        {
            workspace.prepareFrame();
            workspace.record(frame.begin());
            frame.submitAndWait();
            workspace.completeFrame();
            server.collectCompletedFrame();
            auto& graph = workspace.graph();
            return readback(device, graph.graph.getTexture(graph.rendererOutputs.color));
        };
        const auto baseline = render();
        const auto file     = directory / "types.vworkspace";
        workspace.save(file);
        const auto saved = ResearchDocument::load(file);
        workspace.replace(saved);
        require(render().rgba == baseline.rgba, "Reopening a registered-type workspace changed its rendered image");
        const auto rid                                     = workspace.sceneRid();
        auto       invalid                                 = saved;
        auto       scene                                   = nlohmann::json::parse(invalid.sceneSnapshot);
        scene["root"]["children"][0]["extension"]["speed"] = -1;
        invalid.sceneSnapshot                              = scene.dump();
        reject(
            [&]
            {
                invalid.save(file);
            },
            "create extension instance");
        require(ResearchDocument::load(file).sceneSnapshot == saved.sceneSnapshot,
                "Rejected extension state overwrote the valid workspace file");
        reject(
            [&]
            {
                workspace.replace(invalid);
            },
            "create extension instance");
        require(workspace.sceneRid() == rid && render().rgba == baseline.rgba,
                "A failed type snapshot replaced the active workspace");
        workspace.exportImages(directory / "captures");
    }
#endif
} // namespace

int main(int argc, char** argv)
try
{
    require(argc > 0, "Missing executable path");
    const auto binaries  = std::filesystem::absolute(argv[0]).parent_path();
    const auto directory = std::filesystem::absolute("build/.tmp/project-types-" + StableId::generate().toString());
    std::filesystem::create_directories(directory / "code");
    ProjectManifest project;
    project.mainScene = "main.vscene";
    for (const auto* name : {"test-native-types-code", "test-native-types-derived"})
    {
        const auto file = library(binaries, name);
        const auto path = std::filesystem::path("code") / file.filename();
        std::filesystem::copy_file(file, directory / path);
        project.extensions.push_back(project.addAsset(path));
    }
    const auto model = project.addAsset("quad.obj");
    std::ofstream(directory / "quad.obj") << "v -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\n"
                                             "vn 0 0 1\nf 1//1 2//1 3//1\nf 1//1 3//1 4//1\n";
    project.save(directory / "project.vproject");
    AssetSource files(directory);
    NodeId      moverId;
    ResourceId  settingsId;
    {
        ProjectTypes types(project, files);
        SceneTree    world(std::make_unique<Node>("Project world"), types.types());
        const auto   mover    = sceneCreateNodeType(world, nullptr, world.root().id(), "OtherMover", "Mover", "{}");
        const auto   settings = sceneCreateResource(world, "TestSettings", "{}");
        moverId               = world.find(mover)->idInScene();
        settingsId            = world.findResource(settings)->resourceId();
        world.writeProperty(mover, "speed", 4.0f);
        world.writeProperty(settings, "drag", 3.0f);
        world.writeProperty(settings, "target", PropertyReference {moverId.value});
        world.addComponent(mover, "TestVelocity", "{}");
        world.writeComponentProperty(mover, "TestVelocity", "settings", PropertyReference {settingsId.value});
        MeshInstance3D mesh("Quad", model);
        world.addChild(world.root(),
                       types.types().create("TestMesh", mesh.name(), mesh.idInScene(), types.types().serialize(mesh)));
        Camera3D camera("Camera");
        auto&    view = world.addChild(
            world.root(),
            types.types().create("TestCamera", camera.name(), camera.idInScene(), types.types().serialize(camera)));
        static_cast<Camera3D&>(view).setLocalTransform(glm::translate(glm::mat4(1), {0, 0, 3}));
        world.setCurrentCamera(view.id());
        world.save(directory / project.mainScene);
        reject(
            [&]
            {
                SceneTree::load(directory / project.mainScene);
            },
            "OtherMover");
        auto first  = SceneTree::load(directory / project.mainScene, types.types(), &files);
        auto second = SceneTree::load(directory / project.mainScene, types.types(), &files);
        require(first.currentCamera().value != 0 &&
                    static_cast<Camera3D*>(first.find(first.currentCamera()))->localTransform()[3].z == 3,
                "The registered camera lost its authored transform");
        require(first.find(moverId)->id() != second.find(moverId)->id(), "Worlds reused a runtime object identity");
        verifyWorld(first, moverId, settingsId);
        require(second.readProperty(second.find(moverId)->id(), "ticks") == PropertyValue(int64_t {0}),
                "Separate worlds share native instance state");

        auto wrongOrder = project;
        std::swap(wrongOrder.extensions[0], wrongOrder.extensions[1]);
        reject(
            [&]
            {
                ProjectTypes invalid(wrongOrder, files);
            },
            "Load project type module " + wrongOrder.extensions[0].value.toString());
        // A late loader failure must retire the derived module before the base, including during construction.
        auto       partial = project;
        const auto badPath = library("code", "invalid");
        std::ofstream(directory / badPath) << "invalid native module";
        const auto bad = partial.addAsset(badPath);
        partial.extensions.push_back(bad);
        reject(
            [&]
            {
                ProjectTypes invalid(partial, files);
            },
            "Load project type module " + bad.value.toString());
        require(world.readProperty(mover, "speed") == PropertyValue(4.0f),
                "A failed independent project installation damaged the live catalog");
    }
    const auto originalIds = project.extensions;
    std::filesystem::rename(directory / "code", directory / "modules");
    for (const auto id : project.extensions)
    {
        project.renameAsset(id, std::filesystem::path("modules") / project.asset(id).path.filename());
    }
    project.save(directory / "project.vproject");
    std::vector<VpkFile> entries {{"project.vproject", directory / "project.vproject"},
                                  {"main.vscene", directory / project.mainScene}};
    for (const auto& asset : project.assets())
    {
        entries.push_back({asset.path.generic_string(), directory / asset.path});
    }
    const auto package = directory / "project.vpk";
    VpkArchive::pack(entries, package);
    AssetSource packed {VpkArchive(package)};
    for (const auto* source : {&files, &packed})
    {
        const auto restored = ProjectManifest::load(source->resolve("project.vproject"), source);
        require(restored.extensions == originalIds, "Renaming module files changed persistent type module IDs");
        ProjectTypes types(restored, *source);
        auto         world = SceneTree::load(source->resolve(restored.mainScene), types.types(), source);
        verifyWorld(world, moverId, settingsId);
    }
#ifdef TEST_PROJECT_TYPES_RENDERING
    verifyWorkspace(directory);
#endif
    std::cout << "Project type installation passed: ordered dependencies, failure cleanup, independent worlds, "
                 "persistent references, detach, module rename and filesystem/VPK loading\n"
              << "Project fixture: " << directory << '\n';
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
