#include <vultra/api/native_extension.hpp>
#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/scene/render_nodes.hpp>
#include <vultra/scene/scene_api.hpp>
#include <vultra/scene/scene_gpu_sync.hpp>
#include <vultra/scene/scene_import.hpp>
#include <vultra/scene/scene_render_state.hpp>
#include <vultra/servers/rendering/builtin/builtin_renderer.hpp>
#include <vultra/servers/rendering/rendering_server.hpp>

#include <glm/ext/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
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

    SceneData quad()
    {
        SceneData data;
        data.vertices = {{{-1, -1, 0}, {0, 0, 1}, {0, 0}},
                         {{1, -1, 0}, {0, 0, 1}, {1, 0}},
                         {{1, 1, 0}, {0, 0, 1}, {1, 1}},
                         {{-1, 1, 0}, {0, 0, 1}, {0, 1}}};
        data.indices  = {0, 1, 2, 0, 2, 3};
        data.materials.emplace_back();
        data.primitives = {{0, 6, 0}};
        data.radius     = std::sqrt(2.0f);
        return data;
    }

    SceneTree world(const SceneTypeCatalog& types, AssetId model, bool extended)
    {
        SceneTree        tree(std::make_unique<Node>("Render world"), types);
        MaterialResource material("Red");
        auto             parameters = material.parameters();
        parameters.baseRed          = 0.8f;
        parameters.baseGreen        = 0.1f;
        parameters.baseBlue         = 0.05f;
        parameters.specularWeight   = 0;
        material.setParameters(parameters);
        Resource* resource;
        if (extended)
        {
            auto data         = Json::parse(types.serialize(material));
            data["extension"] = Json::object();
            resource = &tree.addResource(types.createResource("TestMaterial", material.resourceId(), data.dump()));
        }
        else
        {
            auto plain = std::make_unique<MaterialResource>("Red", material.resourceId());
            plain->setParameters(parameters);
            resource = &tree.addResource(std::move(plain));
        }
        MeshInstance3D mesh("Quad", model);
        mesh.setMaterial(0, resource->resourceId());
        tree.addChild(
            tree.root(),
            types.create(extended ? "TestMesh" : "MeshInstance3D", "Quad", mesh.idInScene(), types.serialize(mesh)));
        Camera3D camera("Camera");
        auto     settings    = camera.settings();
        settings.verticalFov = 0.85f;
        camera.setSettings(settings);
        auto& view = tree.addChild(
            tree.root(),
            types.create(extended ? "TestCamera" : "Camera3D", "Camera", camera.idInScene(), types.serialize(camera)));
        static_cast<Camera3D&>(view).setLocalTransform(glm::translate(glm::mat4(1), {0, 0, 3}));
        tree.setCurrentCamera(view.id());
        return tree;
    }
} // namespace

int main(int argc, char** argv)
{
    require(argc > 0, "Missing executable path");
    const auto binaries = std::filesystem::absolute(argv[0]).parent_path();
#ifdef _WIN32
    const auto module = binaries / "test-native-types-code.dll";
#else
    const auto module = binaries / "libtest-native-types-code.so";
#endif
    SceneTypeCatalog types;
    NativeExtension  extension(module, types);
    Device           device;
    RenderingServer  server(device);
    const auto       data = quad();
    auto             gpu  = server.uploadScene(data);
    Environment      environment(device);
    Frame            frame(device);
    const AssetId    model {StableId::generate()};
    auto             plain    = world(types, model, false);
    auto             extended = world(types, model, true);
    const auto       directory =
        std::filesystem::path("build/.tmp") / ("native-types-images-" + StableId::generate().toString());
    std::filesystem::create_directories(directory);
    std::cout << "Native extension rendering captures: " << directory << std::endl;
    for (const auto path : {RenderPath::eNaiveForward, RenderPath::eNaiveDeferred})
    {
        BuiltinRenderer renderer(device, *gpu, environment);
        renderer.settings.path             = path;
        renderer.settings.ibl              = false;
        renderer.settings.skybox           = false;
        renderer.settings.shadowFilter     = ShadowFilter::eDisabled;
        renderer.settings.directionToLight = {0, 0, 1};
        renderer.settings.lightIntensity   = 2;
        RenderGraph      graph(device);
        constexpr Extent extent {129, 129};
        const auto       outputs = renderer.addPasses(graph, extent);
        graph.exportResource(outputs.hdr);
        graph.exportResource(outputs.color);
        graph.exportResource(outputs.depth);
        graph.compile();
        const auto* geometry = gpu->vertices->handle;
        const auto* target   = graph.getTexture(outputs.hdr).handle;
        const auto  render   = [&](SceneTree& scene)
        {
            const auto*                    node = scene.root().children()[0];
            std::vector<SceneMeshInstance> instances {{node->idInScene(),
                                                       model,
                                                       glm::mat4(1),
                                                       glm::mat4(1),
                                                       0,
                                                       1,
                                                       0,
                                                       {MaterialParameters::fromMaterial(data.materials.front())}}};
            SceneGpuSync                   sync;
            sync.update(scene, instances, *gpu);
            SceneRenderState state;
            state.update(scene, extent);
            renderer.prepare(*state.camera, graph, outputs, state.lighting(), state.environmentIntensity);
            graph.execute(frame.begin());
            frame.submitAndWait();
            const auto depth = readback(device, graph.getTexture(outputs.depth));
            const auto pixel = (size_t(extent.height / 2) * extent.width + extent.width / 2) * 4;
            require(depth.rgba[pixel] < 1, "Native extension probe missed the test geometry");
            return readback(device, graph.getTexture(outputs.hdr));
        };
        const std::string prefix    = path == RenderPath::eNaiveForward ? "forward" : "deferred";
        const auto        reference = render(plain);
        const auto        actual    = render(extended);
        savePfm(reference, directory / (prefix + "-plain.pfm"));
        savePfm(actual, directory / (prefix + "-native.pfm"));
        savePng(readback(device, graph.getTexture(outputs.color)), directory / (prefix + "-native.png"));
        require(actual.rgba == reference.rgba, "Registered native mesh/camera/material changed HDR rendering");
        const auto center = (size_t(extent.height / 2) * extent.width + extent.width / 2) * 4;
        require(reference.rgba[center] > 0.01f, "Native extension rendering probe missed the geometry");
        auto*      material = extended.materials().front();
        const auto red      = material->parameters();
        auto       blue     = red;
        blue.baseRed        = 0.05f;
        blue.baseGreen      = 0.1f;
        blue.baseBlue       = 0.8f;
        sceneSetMaterialParameters(extended, material->id(), blue);
        require(render(extended).rgba != reference.rgba && gpu->vertices->handle == geometry &&
                    graph.getTexture(outputs.hdr).handle == target,
                "Native material edit missed GPU synchronization or recreated geometry/targets");
        savePng(readback(device, graph.getTexture(outputs.color)), directory / (prefix + "-edited.png"));
        sceneSetMaterialParameters(extended, material->id(), red);
        require(render(extended).rgba == reference.rgba, "Restoring native material parameters changed the image");
        auto loaded = SceneTree::parse(extended.serialize(), types);
        require(render(loaded).rgba == reference.rgba, "Native scene save/reload changed rendering");
    }
    std::cout
        << "Native extension rendering passed: exact HDR parity, live material edits, stable buffers and persistence; "
        << directory << '\n';
}
