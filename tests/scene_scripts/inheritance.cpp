#include <vultra/scripting/native_script.hpp>

#include <nlohmann/json.hpp>

namespace
{
    using namespace vultra::scripting;

    class Mover : public NativeScript
    {
    public:
        static NativeScriptClass definition()
        {
            return scriptClass<Mover>("Mover",
                                      {scriptProperty("speed", &Mover::m_Speed),
                                       scriptProperty("readyCount", &Mover::m_Ready),
                                       scriptProperty("revision", &Mover::m_Revision)},
                                      "Node3D");
        }

        void onReady() override
        {
            ++m_Ready;
        }

        void onProcess(float delta) override
        {
            actor().setPosition({m_Speed * multiplier() * float(m_Revision) * delta, float(m_Ready), 0});
        }

    protected:
        virtual float multiplier() const = 0;

    private:
        float   m_Speed = 2;
        int32_t m_Ready = 0;
#ifdef TEST_INHERITANCE_RELOAD
        int32_t m_Revision = 2;
#else
        int32_t m_Revision = 1;
#endif
    };

    class Ship : public Mover
    {
    public:
        float boost = 2;

    protected:
        float multiplier() const override
        {
            return boost;
        }
    };

    class Scout final : public Ship
    {
    public:
        float altitude = 3;

        void onProcess(float delta) override
        {
            Ship::onProcess(delta);
            auto position = actor().position();
            position.z    = altitude;
            actor().setPosition(position);
        }
    };

    class Observer final : public NativeScript
    {
    public:
        NodeReference target;

        void onProcess(float delta) override
        {
            const auto value = resolve(target).propertyJson("speed");
            actor().setPosition({nlohmann::json::parse(value).get<float>() * delta, 0, 0});
        }
    };

    NativeScriptClass shipDefinition()
    {
#ifdef TEST_INHERITANCE_SHADOW
        return scriptClass<Ship, Mover>("Ship", Mover::definition(), {scriptProperty("speed", &Ship::boost)});
#else
        return scriptClass<Ship, Mover>("Ship", Mover::definition(), {scriptProperty("boost", &Ship::boost)});
#endif
    }
} // namespace

VULTRA_NATIVE_MODULE(
    Mover::definition(),
    shipDefinition(),
    scriptClass<Scout, Ship>("Scout", shipDefinition(), {scriptProperty("altitude", &Scout::altitude)}),
    scriptClass<Observer>("Observer",
                          {scriptProperty("target",
                                          &Observer::target,
                                          {.referenceType = "Node3D", .nullable = false, .referenceClass = "Mover"})},
                          "Node3D"))
