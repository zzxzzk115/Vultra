#include <vultra/scripting/native_script.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <vector>

namespace
{
    using namespace vultra::scripting;

    class ReferenceBeacon final : public NativeScript
    {
    public:
        int32_t energy = 7;
#ifdef TEST_REFERENCES_RELOAD
        int32_t revision = 2;
#else
        int32_t revision = 1;
#endif
    };

    class ReferenceAlien final : public NativeScript
    {};

    class ReferencePilot final : public NativeScript
    {
    public:
        NodeReference              partner;
        std::vector<NodeReference> peers;
        int32_t                    readyEnergy = 0;
        int32_t                    lastEnergy  = 0;

        int32_t energy() const
        {
            const auto peer = resolve(partner);
            return nlohmann::json::parse(peer.propertyJson("energy")).get<int32_t>() *
                   nlohmann::json::parse(peer.propertyJson("revision")).get<int32_t>();
        }

        void onReady() override
        {
            readyEnergy = energy();
        }

        void onProcess(float delta) override
        {
            lastEnergy = energy();
            actor().setPosition({float(lastEnergy) * delta, float(peers.size()), 0});
        }
    };
} // namespace

#ifdef TEST_REFERENCES_BAD
constexpr auto kBeaconClass = "ReferenceIntruder";
#else
constexpr auto kBeaconClass = "ReferenceBeacon";
#endif

VULTRA_NATIVE_MODULE(
    scriptClass<ReferencePilot>(
        "ReferencePilot",
        {scriptProperty("partner",
                        &ReferencePilot::partner,
                        {.referenceType = "Node3D", .nullable = false, .referenceClass = "ReferenceBeacon"}),
         scriptProperty("peers",
                        &ReferencePilot::peers,
                        {.referenceType = "Node3D", .referenceClass = "ReferenceBeacon"}),
         scriptProperty("readyEnergy", &ReferencePilot::readyEnergy),
         scriptProperty("lastEnergy", &ReferencePilot::lastEnergy)},
        "Node3D"),
    scriptClass<ReferenceBeacon>(kBeaconClass,
                                 {scriptProperty("energy", &ReferenceBeacon::energy),
                                  scriptProperty("revision", &ReferenceBeacon::revision)},
                                 "Node3D"),
    scriptClass<ReferenceAlien>("ReferenceAlien", {}, "Node3D"))
