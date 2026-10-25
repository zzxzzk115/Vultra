#include <vultra/scene/node_3d.hpp>
#include <vultra/scene/scene_tree.hpp>
#include <vultra/scripting/script_host.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
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

    void nativeInheritance(const std::filesystem::path& binaries, const std::filesystem::path& directory)
    {
        std::filesystem::create_directories(directory);
        const auto source = directory / library(binaries, "test-native-inheritance").filename();
        std::filesystem::copy_file(library(binaries, "test-native-inheritance"), source);
        SceneTree  world(std::make_unique<Node>("Native inheritance"));
        const auto node = [&](const char* name) -> Node3D&
        {
            return static_cast<Node3D&>(world.addChild(world.root(), std::make_unique<Node3D>(name)));
        };
        auto&      first        = node("Ship");
        auto&      second       = node("Scout");
        auto&      observer     = node("Observer");
        auto&      abstractNode = node("Abstract");
        ScriptHost host(world, true);
        host.addNativeModule(source);
        bool rejected = false;
        try
        {
            host.add({ScriptLanguage::eNative, source, "Mover", abstractNode.idInScene().value});
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        require(rejected && !world.registry().get<SceneProcess>(abstractNode.entity()).behavior,
                "An abstract native script was attached");
        const std::array firstValues {ScriptExport {"speed", 3.0f}};
        const std::array secondValues {ScriptExport {"speed", 5.0f}};
        const std::array observerValues {ScriptExport {"target", PropertyReference {second.idInScene().value}}};
        const std::array attachments {
            ScriptAttachment {ScriptLanguage::eNative, source, "Ship", first.idInScene().value, firstValues},
            ScriptAttachment {ScriptLanguage::eNative, source, "Scout", second.idInScene().value, secondValues},
            ScriptAttachment {ScriptLanguage::eNative, source, "Observer", observer.idInScene().value, observerValues}};
        host.add(attachments);
        const auto& type = host.scriptClass(second.id());
        require(type.baseType == "Node3D" && type.classes.size() == 3 && type.classes[1].name == "Ship" &&
                    type.classes[2].name == "Mover" && type.property("speed").defaultValue == PropertyValue(2.0f),
                "Native script ancestry or inherited private export metadata is wrong");
        host.update(0.25f);
        require(first.localTransform()[3] == glm::vec4(1.5f, 1, 0, 1) &&
                    second.localTransform()[3] == glm::vec4(2.5f, 1, 3, 1) && observer.localTransform()[3].x == 1.25f,
                "Native inherited lifecycle/virtual dispatch or base-class reference did not execute");
        host.writeExport(first.id(), "speed", 7.0f);
        require(host.readExport(second.id(), "speed") == PropertyValue(5.0f),
                "Native base exports share storage between derived instances");
        const auto* before = world.registry().get<SceneProcess>(first.entity()).behavior;
        const auto  stamp  = std::filesystem::last_write_time(source);
        std::filesystem::copy_file(library(binaries, "test-native-inheritance-shadow"),
                                   source,
                                   std::filesystem::copy_options::overwrite_existing);
        std::filesystem::last_write_time(source, stamp + std::chrono::seconds(2));
        require(host.reloadChanged() == 0 && !host.lastReloadError().empty() &&
                    world.registry().get<SceneProcess>(first.entity()).behavior == before &&
                    host.readExport(first.id(), "speed") == PropertyValue(7.0f),
                "Shadowed native base metadata replaced a live inheritance group");
        host.update(0.25f);
        require(first.localTransform()[3].x == 3.5f, "Rejected native inheritance replacement broke old code");
        std::filesystem::copy_file(library(binaries, "test-native-inheritance-replacement"),
                                   source,
                                   std::filesystem::copy_options::overwrite_existing);
        std::filesystem::last_write_time(source, stamp + std::chrono::seconds(4));
        require(host.reloadChanged() == 1 && host.lastReloadError().empty(),
                "Native inheritance group did not recover");
        host.update(0.25f);
        require(first.localTransform()[3].x == 3 && second.localTransform()[3].x == 5 &&
                    host.readExport(first.id(), "speed") == PropertyValue(3.0f),
                "Native inheritance replacement lost initial overrides or retained old virtual methods");
        host.stop();
    }
} // namespace

int main(int argc, char** argv)
try
{
    require(argc > 0, "Missing executable path");
    const auto root = std::filesystem::path("build/.tmp") / ("script-inheritance-" + StableId::generate().toString());
    nativeInheritance(std::filesystem::absolute(argv[0]).parent_path(), root);
    std::cout << "Native inheritance passed: private exports, abstract bases, virtual lifecycle, ancestor references, "
                 "instance isolation and failed/recovered replacement\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
