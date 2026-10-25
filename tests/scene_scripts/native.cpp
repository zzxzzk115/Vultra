#include <vultra/scripting/native_script.hpp>

#include <stdexcept>
#include <vector>

namespace
{
    using namespace vultra::scripting;

    class LightingController final : public NativeScript
    {
    public:
        float              intensityScale = 1;
        std::vector<float> intensitySteps {1, 1};

        void onReady() override
        {
            auto light = actor().createLightChild("Script light", LightKind::ePoint);
            light.setPosition({0, 0, 2});
            auto settings  = light.lightSettings();
            settings.red   = 0.2f;
            settings.green = 0.6f;
            settings.blue  = 1;
            light.setLightSettings(settings);
            auto camera = actor().createCameraChild("Script camera");
            camera.setPosition({0, 0, 3});
            camera.makeCurrent();
            const auto material   = createMaterial("Script material");
            auto       parameters = material.parameters();
            parameters.baseGreen  = 0.25f;
            parameters.baseBlue   = 0.1f;
            material.setParameters(parameters);
            actor().child(0).setMaterial(0, material);
            auto environment = actor().createEnvironmentChild("Script environment");
            environment.makeEnvironmentCurrent();
        }

        void onProcess(float deltaSeconds) override
        {
            auto light    = actor().child(1);
            auto settings = light.lightSettings();
            if (intensitySteps.empty())
            {
                throw std::invalid_argument("Lighting steps require at least one sample");
            }
            float weight = 0;
            for (float value : intensitySteps)
            {
                weight += value;
            }
            settings.intensity = (2 + deltaSeconds) * intensityScale * weight / float(intensitySteps.size());
            light.setLightSettings(settings);
            auto camera            = actor().child(2);
            auto projection        = camera.cameraSettings();
            projection.verticalFov = 0.9f;
            camera.setCameraSettings(projection);
            const auto material   = actor().child(0).material(0).value();
            auto       parameters = material.parameters();
            parameters.baseRed    = 0.4f + deltaSeconds;
            material.setParameters(parameters);
            auto environment = actor().child(3);
            environment.setEnvironmentSettings({1 + deltaSeconds});
        }
    };
} // namespace

VULTRA_NATIVE_MODULE(vultra::scripting::scriptClass<LightingController>(
    "LightingController",
    {vultra::scripting::scriptProperty("intensityScale", &LightingController::intensityScale),
     vultra::scripting::scriptProperty("intensitySteps", &LightingController::intensitySteps)},
    "Node3D"))
