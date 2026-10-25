#include <vultra/assets/scene_render_data.hpp>
#include <vultra/core/base/stable_id.hpp>
#include <vultra/drivers/rhi/image_readback.hpp>
#include <vultra/scene/render_nodes.hpp>
#include <vultra/scene/scene_gpu_sync.hpp>
#include <vultra/servers/rendering/builtin/builtin_renderer.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <array>
#include <iostream>
#include <stdexcept>

namespace
{
    void require(bool value, const char* message)
    {
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }
} // namespace

int main()
try
{
    using namespace vultra;
    const auto directory = std::filesystem::path("build/.tmp") / ("gpu-scene-sync-" + StableId::generate().toString());
    std::filesystem::create_directories(directory);
    Device      device;
    Environment environment(device);
    for (const auto path : {RenderPath::eNaiveForward, RenderPath::eNaiveDeferred})
    {
        SceneData scene;
        scene.vertices   = {{{-0.8f, -0.7f, 0}, {0, 0, 1}, {0, 0}},
                            {{0.8f, -0.7f, 0}, {0, 0, 1}, {1, 0}},
                            {{0, 0.8f, 0}, {0, 0, 1}, {0.5f, 1}}};
        scene.indices    = {0, 1, 2};
        scene.primitives = {{0, 3, 0}};
        scene.materials.emplace_back();
        scene.radius = 1;
        PreparedTextures prepared;
        TextureData      white;
        white.format = TextureFormat::eRgba8Unorm;
        white.levels = {{{1, 1}, std::vector<std::byte>(4, std::byte {255})}};
        prepared.images.push_back(std::move(white));
        prepared.materials.resize(1);
        GpuScene      gpu(device, scene, prepared);
        SceneTree     world(std::make_unique<Node3D>("World"));
        const AssetId model {StableId::generate()};
        auto&         mesh = static_cast<MeshInstance3D&>(
            world.addChild(world.root(), std::make_unique<MeshInstance3D>("Triangle", model)));
        auto& material       = world.addMaterial(std::make_unique<MaterialResource>("Editable"));
        auto  parameters     = material.parameters();
        parameters.baseRed   = 0.75f;
        parameters.baseGreen = 0.2f;
        parameters.baseBlue  = 0.05f;
        material.setParameters(parameters);
        mesh.setMaterial(0, material.resourceId());
        std::array      instances {SceneMeshInstance {mesh.idInScene(),
                                                      model,
                                                      glm::mat4(1),
                                                      glm::mat4(1),
                                                      0,
                                                      1,
                                                      0,
                                                      {MaterialParameters::fromMaterial(scene.materials[0])}}};
        SceneGpuSync    synchronization;
        BuiltinRenderer renderer(device, gpu, environment);
        renderer.settings.path             = path;
        renderer.settings.skybox           = false;
        renderer.settings.ibl              = false;
        renderer.settings.shadowFilter     = ShadowFilter::eDisabled;
        renderer.settings.shadowResolution = 32;
        renderer.settings.debugMode        = 1;
        constexpr Extent   size {129, 97};
        const RenderCamera camera {
            glm::lookAtRH(glm::vec3(0, 0, 2), glm::vec3(0), glm::vec3(0, 1, 0)),
            glm::perspectiveRH_ZO(glm::radians(60.0f), float(size.width) / size.height, 0.1f, 10.0f),
            0.1f,
            10};
        RenderGraph graph(device);
        const auto  outputs = renderer.addPasses(graph, size);
        graph.exportResource(outputs.color);
        if (path == RenderPath::eNaiveDeferred)
        {
            graph.exportResource(outputs.gbuffer[2]);
        }
        graph.compile();
        Frame      submission(device);
        const auto render = [&]
        {
            synchronization.update(world, instances, gpu);
            renderer.prepare(camera, graph, outputs);
            graph.execute(submission.begin());
            submission.submitAndWait();
            renderer.completeFrame();
            return readback(device, graph.getTexture(outputs.color));
        };
        const auto before   = render();
        const auto vertices = gpu.vertices->handle;
        const auto target   = graph.getTexture(outputs.color).handle;
        parameters.baseRed  = 0.05f;
        parameters.baseBlue = 0.75f;
        material.setParameters(parameters);
        mesh.setLocalTransform(glm::translate(glm::mat4(1), {0.3f, 0, 0}));
        const auto after   = render();
        const auto initial = imagePixel(before, size.width / 2, size.height / 2);
        const auto edited  = imagePixel(after, size.width / 2, size.height / 2);
        require(initial[0] > initial[2] + 0.2f && edited[2] > edited[0] + 0.2f && before.rgba != after.rgba,
                "World material and transform edits did not reach GPU pixels");
        require(gpu.vertices->handle == vertices && graph.getTexture(outputs.color).handle == target,
                "World edits recreated geometry or graph output resources");
        mesh.setModel({StableId::generate()});
        bool rejected = false;
        try
        {
            synchronization.update(world, instances, gpu);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(rejected, "GPU synchronization accepted mismatched geometry topology");
        mesh.setModel(model);
        require(render().rgba == after.rgba, "Rejected topology update damaged the live renderer");
        const auto name = path == RenderPath::eNaiveForward ? "forward" : "deferred";
        savePng(before, directory / (std::string(name) + "-before.png"));
        savePng(after, directory / (std::string(name) + "-after.png"));
        if (path == RenderPath::eNaiveDeferred)
        {
            savePng(readback(device, graph.getTexture(outputs.gbuffer[2])), directory / "albedo.png");
        }
    }
    std::cout << "World/GPU synchronization passed: both paths, live edits, reuse and rejection/recovery; " << directory
              << '\n';
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
