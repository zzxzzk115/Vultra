#include <vultra/scene/node_3d.hpp>
#include <vultra/scene/scene_tree.hpp>
#include <vultra/scripting/script_host.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
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

    std::filesystem::path library(const std::filesystem::path& binaries, const char* name)
    {
#ifdef _WIN32
        return binaries / (std::string(name) + ".dll");
#else
        return binaries / ("lib" + std::string(name) + ".so");
#endif
    }

    void writeLua(const std::filesystem::path& path, bool replacement, bool narrow)
    {
        std::ofstream file(path);
        file << "local Script = { class_name='ExportProbe', extends='Node3D', exports={"
                "speed={kind='float',default="
             << (replacement ? 4 : 1) << "}, steps={kind='int',integer_bits=" << (narrow ? 8 : 32)
             << ",default=0}}}\n"
                "function Script:_process(delta)\n"
                "  local x,y,z=self.node:position()\n"
                "  self.node:set_position(x+self.speed*delta,y,z)\n"
                "  self.steps=self.steps+1\n"
                "end\nreturn Script\n";
        file.close();
        require(bool(file), "Write grouped Lua replacement fixture");
    }

    void groupedReplacement(ScriptLanguage               language,
                            const std::filesystem::path& binaries,
                            const std::filesystem::path& directory)
    {
        const bool managed = language == ScriptLanguage::eCSharp;
        const auto speed   = managed ? "Speed" : "speed";
        const auto steps   = managed ? "Steps" : "steps";
        const auto type    = managed ? "VultraTests.ExportProbe" : "ExportProbe";
        std::filesystem::create_directories(directory);
        std::filesystem::path source;
        std::filesystem::path narrow;
        std::filesystem::path replacement;
        if (language == ScriptLanguage::eNative)
        {
            source      = library(binaries, "test-native-export-script");
            narrow      = library(binaries, "test-native-export-script-narrow");
            replacement = library(binaries, "test-native-export-script-replacement");
        }
        else if (managed)
        {
            source      = "build/.tmp/scripting-managed/Vultra.SceneScripts.dll";
            narrow      = "build/.tmp/scripting-managed-narrow/Vultra.SceneScripts.dll";
            replacement = "build/.tmp/scripting-managed-identity-replacement/Vultra.SceneScripts.dll";
            for (const auto& file : std::filesystem::directory_iterator(source.parent_path()))
            {
                if (file.is_regular_file())
                {
                    std::filesystem::copy_file(file.path(), directory / file.path().filename());
                }
            }
        }
        else
        {
            source      = directory / "initial.lua";
            narrow      = directory / "narrow.lua";
            replacement = directory / "replacement.lua";
            writeLua(source, false, false);
            writeLua(narrow, true, true);
            writeLua(replacement, true, false);
        }
        const auto active    = directory / ("active" + source.extension().string());
        const auto unrelated = directory / ("unrelated" + source.extension().string());
        std::filesystem::copy_file(source, active);
        std::filesystem::copy_file(source, unrelated);
        const auto stamp = std::filesystem::last_write_time(active);
        SceneTree  world(std::make_unique<Node>("Grouped reload"));
        auto&      first  = static_cast<Node3D&>(world.addChild(world.root(), std::make_unique<Node3D>("First")));
        auto&      second = static_cast<Node3D&>(world.addChild(world.root(), std::make_unique<Node3D>("Second")));
        auto&      third  = static_cast<Node3D&>(world.addChild(world.root(), std::make_unique<Node3D>("Unrelated")));
        ScriptHost host(world, true);
        const std::array firstValues {ScriptExport {speed, 2.0f}, ScriptExport {steps, int64_t {7}}};
        const std::array secondValues {ScriptExport {speed, 3.0f}, ScriptExport {steps, int64_t {300}}};
        host.add({language, active, type, first.idInScene().value, firstValues});
        host.add({language, directory / "." / active.filename(), type, second.idInScene().value, secondValues});
        host.add({language, unrelated, type, third.idInScene().value});
        host.update(0.25f);
        require(first.localTransform()[3].x == 0.5f && second.localTransform()[3].x == 0.75f,
                "Grouped reload fixture did not execute the original scripts");
        host.writeExport(first.id(), speed, 5.0f);
        host.writeExport(first.id(), steps, int64_t {50});
        host.update(0.25f);
        const auto* firstBehavior  = world.registry().get<SceneProcess>(first.entity()).behavior;
        const auto* secondBehavior = world.registry().get<SceneProcess>(second.entity()).behavior;
        std::filesystem::copy_file(narrow, active, std::filesystem::copy_options::overwrite_existing);
        std::filesystem::last_write_time(active, stamp + std::chrono::seconds(2));
        require(host.reloadChanged() == 0 && host.lastReloadError().find("out of range") != std::string_view::npos,
                "Invalid second-instance exports did not reject the entire module group");
        require(world.registry().get<SceneProcess>(first.entity()).behavior == firstBehavior &&
                    world.registry().get<SceneProcess>(second.entity()).behavior == secondBehavior &&
                    host.scriptClass(first.id()).property(steps).integerBits == 32 &&
                    host.readExport(first.id(), speed) == PropertyValue(5.0f) &&
                    host.readExport(first.id(), steps) == PropertyValue(int64_t {51}),
                "Failed staging replaced the first instance or discarded its live edits");
        require(host.reloadChanged(true) == 0, "Retry committed a partial replacement");
        host.update(0.25f);
        require(first.localTransform()[3].x == 3 && second.localTransform()[3].x == 2.25f &&
                    host.readExport(second.id(), steps) == PropertyValue(int64_t {303}),
                "Failed module staging stopped or mixed the two previous script instances");
        std::filesystem::copy_file(replacement, active, std::filesystem::copy_options::overwrite_existing);
        std::filesystem::last_write_time(active, stamp + std::chrono::seconds(4));
        require(host.reloadChanged() == 2 && host.lastReloadError().empty() &&
                    host.scriptClass(first.id()).property(speed).defaultValue == PropertyValue(4.0f) &&
                    host.scriptClass(second.id()).property(speed).defaultValue == PropertyValue(4.0f) &&
                    host.scriptClass(third.id()).property(speed).defaultValue == PropertyValue(1.0f) &&
                    host.readExport(first.id(), speed) == PropertyValue(2.0f) &&
                    host.readExport(second.id(), steps) == PropertyValue(int64_t {300}),
                "Successful group recovery missed an instance, changed another module or lost initial exports");
        host.update(0.25f);
        require(first.localTransform()[3].x == 3.5f && second.localTransform()[3].x == 3 &&
                    third.localTransform()[3].x == 1 &&
                    host.readExport(first.id(), steps) == PropertyValue(int64_t {8}),
                "Recovered module group did not execute with independent instance values");
        host.stop();
    }

    void activationFailure(const std::filesystem::path& binaries, const std::filesystem::path& directory)
    {
        std::filesystem::create_directories(directory);
        const auto source = library(binaries, "test-native-export-script");
        const auto active = directory / source.filename();
        std::filesystem::copy_file(source, active);
        const auto       stamp = std::filesystem::last_write_time(active);
        SceneTree        world(std::make_unique<Node3D>("Activation failure"));
        ScriptHost       host(world, true);
        const std::array initial {ScriptExport {"speed", 2.0f}};
        host.add({ScriptLanguage::eNative, active, "ExportProbe", world.root().idInScene().value, initial});
        host.update(0.25f);
        std::filesystem::copy_file(library(binaries, "test-native-export-script-activation-failure"),
                                   active,
                                   std::filesystem::copy_options::overwrite_existing);
        std::filesystem::last_write_time(active, stamp + std::chrono::seconds(2));
        bool rejected = false;
        try
        {
            host.update(0.25f);
        }
        catch (const std::runtime_error& error)
        {
            rejected = std::string_view(error.what()).find("Plugin scene callback failed") != std::string_view::npos;
        }
        require(rejected && !world.running() && host.lastReloadError().starts_with("Activate reloaded ") &&
                    host.scriptClass(world.root().id()).property("speed").defaultValue == PropertyValue(4.0f) &&
                    static_cast<Node3D&>(world.root()).localTransform()[3].x == 0.5f,
                "Post-commit activation failure was hidden or allowed the next process callback to run");
        std::filesystem::copy_file(library(binaries, "test-native-export-script-replacement"),
                                   active,
                                   std::filesystem::copy_options::overwrite_existing);
        std::filesystem::last_write_time(active, stamp + std::chrono::seconds(4));
        require(host.reloadChanged() == 1 && host.lastReloadError().empty(), "Activation failure poisoned recovery");
        host.update(0.25f);
        require(world.running() && static_cast<Node3D&>(world.root()).localTransform()[3].x == 1,
                "A repaired activation failure did not resume the configured script logic");
    }
} // namespace

int main(int argc, char** argv)
{
    require(argc > 0, "Missing executable path");
    const auto binaries  = std::filesystem::absolute(argv[0]).parent_path();
    const auto directory = std::filesystem::path("build/.tmp") / ("script-reload-" + StableId::generate().toString());
    for (const auto language : {ScriptLanguage::eNative, ScriptLanguage::eLua, ScriptLanguage::eCSharp})
    {
        groupedReplacement(language, binaries, directory / std::to_string(int(language)));
    }
    activationFailure(binaries, directory / "activation");
    std::cout << "Grouped reload passed: all-instance staging/rejection/retry, independent modules and Native/Lua/C# "
                 "recovery\n";
}
