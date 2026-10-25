#include <vultra/assets/source_file.hpp>
#include <vultra/platform/os/file.hpp>
#include <vultra/scene/node_3d.hpp>
#include <vultra/scene/scene_tree.hpp>
#include <vultra/scripting/script_host.hpp>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>

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

    void run(ScriptLanguage language, const std::filesystem::path& binaries, const std::filesystem::path& directory)
    {
        std::filesystem::create_directories(directory);
        std::filesystem::path path;
        const bool            managed = language == ScriptLanguage::eCSharp;
        const bool            lua     = language == ScriptLanguage::eLua;
        if (lua)
        {
            path = directory / "motion.lua";
            std::filesystem::copy_file("tests/scene_scripts/random.lua", path);
        }
        else if (managed)
        {
            const auto source = std::filesystem::absolute("build/.tmp/scripting-managed");
            for (const auto& file : std::filesystem::directory_iterator(source))
            {
                if (file.is_regular_file())
                {
                    std::filesystem::copy_file(file.path(), directory / file.path().filename());
                }
            }
            path = directory / "Vultra.SceneScripts.dll";
        }
        else
        {
#ifdef _WIN32
            const auto source = binaries / "test-native-random-script.dll";
#else
            const auto source = binaries / "libtest-native-random-script.so";
#endif
            path = directory / source.filename();
            std::filesystem::copy_file(source, path);
        }
        const auto field = [managed](std::string name)
        {
            if (managed)
            {
                name[0] = char(name[0] - 'a' + 'A');
            }
            return name;
        };
        SceneTree world(std::make_unique<Node3D>("Random actor"));
        SceneTree reference(std::make_unique<Node>("Reference"));
        world.setRandomSeed(73);
        reference.setRandomSeed(73);
        ScriptHost host(world, true);
        host.add({language, path, managed ? "VultraTests.RandomMotion" : "RandomMotion"});
        auto step = [&]
        {
            const auto word  = reference.randomUInt("motion.word");
            const auto index = reference.randomUInt("motion.index", 7);
            const auto unit  = reference.randomUnit("motion.unit");
            host.update(0.25f);
            require(host.readExport(world.root().id(), field("seed")) == PropertyValue(int64_t {73}),
                    "Script seed diagnostic differs from the world's configured seed");
            require(host.readExport(world.root().id(), field("word")) == PropertyValue(int64_t(word)),
                    "Script random word differs from the common native world sequence");
            require(host.readExport(world.root().id(), field("index")) == PropertyValue(int64_t(index)),
                    "Script random index differs from the common native world sequence");
            require(host.readExport(world.root().id(), field("unit")) == PropertyValue(unit),
                    "Script random unit differs from the common native world sequence");
            const auto translation = static_cast<Node3D&>(world.root()).localTransform()[3];
            require(translation.x == float(unit * 0.5) && translation.y == float(double(index) * 0.02),
                    "Script random output did not drive its actual transform");
            require(world.randomState() == reference.randomState(),
                    "Language callbacks consumed unexpected random draws");
        };
        for (int i = 0; i < 5; ++i)
        {
            step();
        }
        const auto        before   = world.randomState();
        const auto        contents = readSourceFile(path);
        const auto        stamp    = std::filesystem::last_write_time(path);
        const std::string invalid  = "invalid script or module";
        writeFileAtomically(path, std::as_bytes(std::span(invalid)));
        std::filesystem::last_write_time(path, stamp + std::chrono::seconds(2));
        require(host.reloadChanged() == 0 && !host.lastReloadError().empty() && world.randomState() == before,
                "Failed script replacement reset or consumed the live world's random state");
        step();
        writeFileAtomically(path, contents);
        std::filesystem::last_write_time(path, stamp + std::chrono::seconds(4));
        const auto recovered = world.randomState();
        require(host.reloadChanged(true) == 1 && world.randomState() == recovered,
                "Successful code replacement reset the world's random streams");
        step();
        host.stop();
    }
} // namespace

int main(int argc, char** argv)
try
{
    require(argc > 0, "Missing executable path");
    const auto root = std::filesystem::absolute("build/.tmp/script-random-" + StableId::generate().toString());
    for (const auto language : {ScriptLanguage::eNative, ScriptLanguage::eLua, ScriptLanguage::eCSharp})
    {
        run(language, std::filesystem::absolute(argv[0]).parent_path(), root / std::to_string(int(language)));
    }
    std::cout << "Native/Lua/C# random sequence, transform and failed/recovered code replacement parity passed\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
