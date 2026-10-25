#include <vultra/scene/node_3d.hpp>
#include <vultra/scene/scene_tree.hpp>
#include <vultra/scripting/script_host.hpp>

#include <algorithm>
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

    void dependencies(bool ownerFirst, const std::filesystem::path& directory)
    {
        const auto fixtures = std::filesystem::path("build/.tmp/managed-dependency-fixtures");
        std::filesystem::create_directories(directory);
        for (const auto& file : std::filesystem::directory_iterator(fixtures / "initial"))
        {
            if (file.is_regular_file())
            {
                std::filesystem::copy_file(file.path(), directory / file.path().filename());
            }
        }
        const auto controller = directory / "DependencyController.dll";
        const auto beacon     = directory / "DependencyBeacon.dll";
        const auto helper     = directory / "DependencyHelper.dll";
        const auto sensor     = directory / "DependencySensor.dll";
        const auto observer   = directory / "DependencyObserver.dll";
        SceneTree  world(std::make_unique<Node>("Assembly dependencies"));
        const auto node = [&](const char* name) -> Node3D&
        {
            return static_cast<Node3D&>(world.addChild(world.root(), std::make_unique<Node3D>(name)));
        };
        auto&      owner   = node("Controller");
        auto&      first   = node("Beacon");
        auto&      second  = node("Derived beacon");
        auto&      extra   = node("Independent sensor");
        auto&      reverse = node("Sensor observer");
        const auto module  = [](const char*                   type,
                                const Node&                   target,
                                const std::filesystem::path&  path,
                                std::span<const ScriptExport> values = {})
        {
            return ScriptAttachment {ScriptLanguage::eCSharp, path, type, target.idInScene().value, values};
        };
        const std::array ownerValues {ScriptExport {"Target", PropertyReference {first.idInScene().value}},
                                      ScriptExport {"Peers",
                                                    PropertyArray {{PropertyReference {first.idInScene().value},
                                                                    PropertyReference {},
                                                                    PropertyReference {second.idInScene().value}}}}};
        const std::array firstValues {ScriptExport {"Power", int64_t {11}}};
        const std::array extraValues {ScriptExport {"Signal", int64_t {9}}};
        const std::array reverseValues {ScriptExport {"Target", PropertyReference {extra.idInScene().value}}};
        std::array       attachments {module("DependencyTests.Controller", owner, controller, ownerValues),
                                      module("DependencyTests.Beacon", first, beacon, firstValues),
                                      module("DependencyTests.SpecialBeacon", second, beacon),
                                      module("DependencyTests.Sensor", extra, sensor, extraValues),
                                      module("DependencyTests.Observer", reverse, observer, reverseValues)};
        if (!ownerFirst)
        {
            std::swap(attachments[0], attachments[1]);
        }
        ScriptHost host(world, true);
        host.add(attachments);
        const auto& field = host.scriptClass(owner.id()).property("Target");
        require(field.referenceClass &&
                    field.referenceClass->module == std::filesystem::canonical(beacon).generic_string() &&
                    field.referenceClass->name == "DependencyTests.Beacon" &&
                    world.behaviorClasses(owner.id()).size() == 2 &&
                    world.behaviorClasses(owner.id())[1].module == std::filesystem::canonical(beacon).generic_string(),
                "External script fields or inheritance lost their original assembly identity");
        const auto& sources = host.scriptClass(owner.id()).dependencies;
        require(std::ranges::find(sources, std::filesystem::canonical(helper).generic_string()) != sources.end(),
                "A helper-only IL dependency was absent from the script source set");
        host.update(0.25f);
        require(owner.localTransform()[3] == glm::vec4(2.75f, 3, 3, 1) &&
                    host.readExport(owner.id(), "ReadyPower") == PropertyValue(int64_t {11}) &&
                    reverse.localTransform()[3].x == 2.25f,
                "Cross-assembly fields, arrays, inherited private exports or direct methods did not execute");
        const auto* oldOwner    = world.registry().get<SceneProcess>(owner.entity()).behavior;
        const auto* oldSensor   = world.registry().get<SceneProcess>(extra.entity()).behavior;
        const auto* oldObserver = world.registry().get<SceneProcess>(reverse.entity()).behavior;
        host.writeExport(first.id(), "Power", int64_t {17});
        const auto stamp = std::filesystem::last_write_time(helper);
        {
            std::ofstream file(helper, std::ios::binary | std::ios::trunc);
            file << "invalid managed image";
        }
        std::filesystem::last_write_time(helper, stamp + std::chrono::seconds(2));
        require(host.reloadChanged() == 0 && !host.lastReloadError().empty() && host.reloadChanged() == 0 &&
                    host.reloadChanged(true) == 0 &&
                    world.registry().get<SceneProcess>(owner.entity()).behavior == oldOwner &&
                    host.readExport(first.id(), "Power") == PropertyValue(int64_t {17}),
                "A failed helper replacement partially published its dependent scripts");
        host.update(0.25f);
        require(owner.localTransform()[3].x == 4.25f, "Failed helper replacement broke the old managed code");
        std::filesystem::copy_file(fixtures / "helper/DependencyHelper.dll",
                                   helper,
                                   std::filesystem::copy_options::overwrite_existing);
        std::filesystem::last_write_time(helper, stamp + std::chrono::seconds(4));
        require(host.reloadChanged() == 3 && host.lastReloadError().empty() &&
                    world.registry().get<SceneProcess>(extra.entity()).behavior == oldSensor &&
                    world.registry().get<SceneProcess>(reverse.entity()).behavior == oldObserver,
                "A helper-only edit missed a dependent root or replaced an independent source");
        host.update(0.25f);
        require(owner.localTransform()[3].x == 5.5f, "Cross-assembly references kept the retired helper code");
        const auto manifest      = directory / "DependencyController.deps.json";
        const auto manifestStamp = std::filesystem::last_write_time(manifest);
        {
            std::ofstream file(manifest, std::ios::app);
            file << '\n';
        }
        std::filesystem::last_write_time(manifest, manifestStamp + std::chrono::seconds(2));
        require(host.reloadChanged() == 3 && host.lastReloadError().empty() &&
                    world.registry().get<SceneProcess>(extra.entity()).behavior == oldSensor,
                "Dependency manifest edits were not watched with their assembly group");
        host.update(0.25f);
        require(owner.localTransform()[3].x == 5.5f, "Manifest replacement changed the managed reference identities");
        const auto controllerStamp = std::filesystem::last_write_time(controller);
        std::filesystem::copy_file(fixtures / "sensor/DependencyController.dll",
                                   controller,
                                   std::filesystem::copy_options::overwrite_existing);
        std::filesystem::copy_file(fixtures / "sensor/DependencyController.deps.json",
                                   directory / "DependencyController.deps.json",
                                   std::filesystem::copy_options::overwrite_existing);
        std::filesystem::last_write_time(controller, controllerStamp + std::chrono::seconds(2));
        require(host.reloadChanged() == 5 && host.lastReloadError().empty() &&
                    world.registry().get<SceneProcess>(extra.entity()).behavior != oldSensor &&
                    world.registry().get<SceneProcess>(reverse.entity()).behavior != oldObserver,
                "A new assembly dependency did not include its existing reverse users");
        world.writeProperty(owner.id(), "Sensor", PropertyReference {extra.idInScene().value});
        host.update(0.25f);
        require(owner.localTransform()[3].x == 7.75f && reverse.localTransform()[3].x == 2.25f,
                "A newly referenced script type belongs to the wrong collectible assembly generation");
        std::filesystem::remove(helper);
        require(host.reloadChanged() == 0 && !host.lastReloadError().empty() && host.reloadChanged() == 0,
                "A missing helper was ignored or repeatedly retried without a source change");
        host.update(0.25f);
        require(owner.localTransform()[3].x == 7.75f, "A missing helper invalidated already loaded code");
        std::filesystem::copy_file(fixtures / "initial/DependencyHelper.dll", helper);
        std::filesystem::last_write_time(helper, stamp + std::chrono::seconds(6));
        require(host.reloadChanged() == 5 && host.lastReloadError().empty(),
                "A restored dependency did not recover the expanded group");
        host.update(0.25f);
        require(owner.localTransform()[3].x == 2.75f, "Recovered sources did not reapply authored exports");
        host.stop();
    }
} // namespace

int main()
try
{
    const auto root = std::filesystem::path("build/.tmp") / ("managed-dependencies-" + StableId::generate().toString());
    dependencies(true, root / "owner-first");
    dependencies(false, root / "target-first");
    std::cout << "Managed dependencies passed: both attachment orders, external inheritance and typed fields/arrays, "
                 "helper-only reload, failed staging, group expansion and missing-helper recovery\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
