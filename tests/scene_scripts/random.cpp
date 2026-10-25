#include <vultra/scripting/native_script.hpp>

namespace
{
    class RandomMotion final : public vultra::scripting::NativeScript
    {
    public:
        int64_t seed  = 0;
        int64_t word  = 0;
        int64_t index = 0;
        double  unit  = 0;

        void onReady() override
        {
            seed = randomSeed();
        }

        void onProcess(float) override
        {
            seed  = randomSeed();
            word  = randomUInt("motion.word");
            index = randomUInt("motion.index", 7);
            unit  = randomUnit("motion.unit");
            actor().setPosition({float(unit * 0.5), float(double(index) * 0.02), 0});
        }
    };
} // namespace

VULTRA_NATIVE_MODULE(
    vultra::scripting::scriptClass<RandomMotion>("RandomMotion",
                                                 {vultra::scripting::scriptProperty("seed", &RandomMotion::seed),
                                                  vultra::scripting::scriptProperty("word", &RandomMotion::word),
                                                  vultra::scripting::scriptProperty("index", &RandomMotion::index),
                                                  vultra::scripting::scriptProperty("unit", &RandomMotion::unit)},
                                                 "Node3D"))
