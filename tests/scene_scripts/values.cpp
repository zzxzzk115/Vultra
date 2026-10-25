#include <vultra/scripting/native_script.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace
{
    enum class Movement : uint8_t
    {
        eIdle   = 0,
        eCruise = 2
    };

    class ValueProbe final : public vultra::scripting::NativeScript
    {
    public:
        Movement                                      mode = Movement::eIdle;
        std::vector<int16_t>                          samples {1, 2};
        std::vector<Movement>                         modes {Movement::eIdle, Movement::eCruise};
        vultra::scripting::Float2                     uv {0.25f, 0.75f};
        vultra::scripting::Float3                     offset {1, 2, 3};
        vultra::scripting::Float4                     tint {1, 0, 0, 1};
        vultra::scripting::Float4x4                   basis;
        std::vector<vultra::scripting::Float3>        vectors {{0, 0, 0}, {1, 1, 1}};
        vultra::scripting::NodeReference              target;
        std::vector<vultra::scripting::NodeReference> peers;
        vultra::scripting::ResourceReference          material;
        std::string                                   readyTarget;
#ifdef TEST_VALUES_RELOAD
        float revision = 4;
#else
        float revision = 1;
#endif

        void onReady() override
        {
            if (target.id)
            {
                readyTarget = resolve(target).name();
            }
        }

        void onProcess(float) override
        {
            if (!target.id)
            {
                readyTarget = "cleared";
                return;
            }
            if (mode != Movement::eCruise)
            {
                return;
            }
            float sum = offset.x + basis.columns[12] + vectors.at(0).x;
            for (const auto value : samples)
            {
                sum += value;
            }
            const auto label = nlohmann::json::parse(resolve(material).propertyJson("name")).get<std::string>();
            actor().setPosition({sum,
                                 float(resolve(target).name().size() + label.size() + peers.size() + int(modes.at(0))),
                                 offset.z + revision});
        }
    };

    class RequiredProbe final : public vultra::scripting::NativeScript
    {
    public:
        vultra::scripting::NodeReference target;
    };
} // namespace

#ifdef TEST_VALUES_BAD
VULTRA_NATIVE_MODULE(vultra::scripting::scriptClass<ValueProbe>("ValueProbe",
                                                                {vultra::scripting::scriptProperty("mode",
                                                                                                   &ValueProbe::mode)},
                                                                "Node3D"))
#else
VULTRA_NATIVE_MODULE(
    vultra::scripting::scriptClass<ValueProbe>(
        "ValueProbe",
        {vultra::scripting::scriptProperty("mode",
                                           &ValueProbe::mode,
                                           {.choices = {{"Idle", Movement::eIdle}, {"Cruise", Movement::eCruise}}}),
         vultra::scripting::scriptProperty("samples", &ValueProbe::samples),
         vultra::scripting::scriptProperty("modes",
                                           &ValueProbe::modes,
                                           {.choices = {{"Idle", Movement::eIdle}, {"Cruise", Movement::eCruise}}}),
         vultra::scripting::scriptProperty("uv", &ValueProbe::uv),
         vultra::scripting::scriptProperty("offset", &ValueProbe::offset),
         vultra::scripting::scriptProperty("tint", &ValueProbe::tint),
         vultra::scripting::scriptProperty("basis", &ValueProbe::basis),
         vultra::scripting::scriptProperty("vectors", &ValueProbe::vectors),
         vultra::scripting::scriptProperty("target", &ValueProbe::target, {.referenceType = "Node3D"}),
         vultra::scripting::scriptProperty("peers", &ValueProbe::peers, {.referenceType = "Node3D"}),
         vultra::scripting::scriptProperty("material", &ValueProbe::material, {.referenceType = "MaterialResource"}),
         vultra::scripting::scriptProperty("readyTarget", &ValueProbe::readyTarget),
         vultra::scripting::scriptProperty("revision", &ValueProbe::revision)},
        "Node3D"),
    vultra::scripting::scriptClass<RequiredProbe>("RequiredProbe",
                                                  {vultra::scripting::scriptProperty("target",
                                                                                     &RequiredProbe::target,
                                                                                     {.referenceType = "Node3D",
                                                                                      .nullable      = false})},
                                                  "Node3D"))
#endif
