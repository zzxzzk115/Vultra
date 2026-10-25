#include <vultra/scene/material_resource.hpp>
#include <vultra/scene/node_3d.hpp>
#include <vultra/scene/scene_api.hpp>
#include <vultra/scene/scene_tree.hpp>
#include <vultra/scripting/script_host.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using namespace vultra;

    void require(bool value, const char* message)
    {
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }

    template<class Operation>
    void reject(Operation operation, const char* message, std::string_view cause = {})
    {
        try
        {
            operation();
        }
        catch (const std::exception& error)
        {
            require(cause.empty() || std::string_view(error.what()).find(cause) != std::string_view::npos,
                    "Rejected a script value for the wrong reason");
            return;
        }
        throw std::runtime_error(message);
    }

    void run(ScriptLanguage language, const std::filesystem::path& binaries, const std::filesystem::path& root)
    {
        const bool managed = language == ScriptLanguage::eCSharp;
        const auto field   = [managed](const char* name)
        {
            std::string result(name);
            if (managed)
            {
                result[0] = char(result[0] - 'a' + 'A');
            }
            return result;
        };
        const auto directory = root / std::to_string(int(language));
        std::filesystem::create_directories(directory);
        std::filesystem::path source;
        std::filesystem::path replacement;
        if (language == ScriptLanguage::eNative)
        {
#ifdef _WIN32
            source      = binaries / "test-native-value-script.dll";
            replacement = binaries / "test-native-value-script-replacement.dll";
#else
            source      = binaries / "libtest-native-value-script.so";
            replacement = binaries / "libtest-native-value-script-replacement.so";
#endif
        }
        else if (managed)
        {
            source      = "build/.tmp/scripting-managed/Vultra.SceneScripts.dll";
            replacement = "build/.tmp/scripting-managed-identity-replacement/Vultra.SceneScripts.dll";
            for (const auto& entry : std::filesystem::directory_iterator(source.parent_path()))
            {
                if (entry.is_regular_file())
                {
                    std::filesystem::copy_file(entry.path(),
                                               directory / entry.path().filename(),
                                               std::filesystem::copy_options::overwrite_existing);
                }
            }
        }
        else
        {
            source      = "tests/scene_scripts/values.lua";
            replacement = directory / "replacement.lua";
            std::ifstream input(source);
            std::string   text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            const auto    position = text.find("revision={kind='float',default=1}");
            require(position != std::string::npos, "Lua replacement fixture has no declared revision");
            text.replace(position,
                         std::string("revision={kind='float',default=1}").size(),
                         "revision={kind='float',default=4}");
            std::ofstream output(replacement);
            output << text;
            require(bool(output), "Write Lua value replacement");
        }
        const auto copy = directory / ("active" + source.extension().string());
        std::filesystem::copy_file(source, copy);
        const auto stamp = std::filesystem::last_write_time(copy);
        SceneTree  world(std::make_unique<Node>("Value world"));
        auto&      first    = world.addChild(world.root(), std::make_unique<Node3D>("First"));
        auto&      second   = world.addChild(world.root(), std::make_unique<Node3D>("Second"));
        auto&      plain    = world.addChild(world.root(), std::make_unique<Node>("Plain"));
        auto&      material = static_cast<MaterialResource&>(
            world.addResource(std::make_unique<MaterialResource>("Shared", ResourceId {first.idInScene().value})));
        SceneTree  foreign(std::make_unique<Node3D>("Foreign"));
        const auto firstId  = first.id();
        const auto secondId = second.id();
        reject(
            [&]
            {
                sceneObjectPersistentId(world, material.id(), "Node");
            },
            "Node/resource runtime identities were confused");
        reject(
            [&]
            {
                sceneObjectPersistentId(world, foreign.root().id(), "Node");
            },
            "Foreign runtime identity was accepted");
        require(sceneFindNode(world, first.idInScene().value.toString()) == first.id(),
                "Stable node lookup lost local identity");
        ScriptHost        host(world, true);
        const std::string type = managed ? "VultraTests.ValueProbe" : "ValueProbe";
        glm::mat4         basis(1);
        basis[3].x  = 4;
        basis[0][1] = 2;
        basis[2][0] = 3;
        const std::vector<ScriptExport> initial {
            {field("mode"), PropertyEnum {2}},
            {field("samples"), PropertyArray {{int64_t {3}, int64_t {4}}}},
            {field("offset"), glm::vec3 {2, 3, 4}},
            {field("basis"), basis},
            {field("vectors"), PropertyArray {{glm::vec3 {1, 0, 0}}}},
            {field("target"), PropertyReference {second.idInScene().value}},
            {field("peers"), PropertyArray {{PropertyReference {second.idInScene().value}, PropertyReference {}}}},
            {field("material"), PropertyReference {material.resourceId().value}}};
        const std::array secondInitial {
            ScriptExport {field("mode"), PropertyEnum {2}},
            ScriptExport {field("target"), PropertyReference {first.idInScene().value}},
            ScriptExport {field("peers"), PropertyArray {{PropertyReference {first.idInScene().value}}}},
            ScriptExport {field("material"), PropertyReference {material.resourceId().value}}};
        host.add({language, copy, type, first.idInScene().value, initial});
        host.add({language, copy, type, second.idInScene().value, secondInitial});
        const auto requiredPath = directory / ("required" + source.extension().string());
        if (language == ScriptLanguage::eLua)
        {
            std::ofstream file(requiredPath);
            file << "return {class_name='RequiredProbe',extends='Node3D',exports={target={kind='reference',"
                    "reference={category='node',type='Node3D',nullable=false}}}}\n";
            require(bool(file), "Write required-reference Lua fixture");
        }
        else
        {
            std::filesystem::copy_file(source, requiredPath);
        }
        auto&      required     = world.addChild(world.root(), std::make_unique<Node3D>("Required"));
        const auto requiredType = managed ? "VultraTests.RequiredProbe" : "RequiredProbe";
        reject(
            [&]
            {
                host.add({language, requiredPath, requiredType, required.idInScene().value});
            },
            "A required reference was activated without an initial value",
            "non-null reference");
        require(!world.registry().get<SceneProcess>(required.entity()).behavior,
                "Failed required reference attached a behavior");
        const std::array requiredValues {ScriptExport {field("target"), PropertyReference {first.idInScene().value}}};
        host.add({language, requiredPath, requiredType, required.idInScene().value, requiredValues});
        if (language == ScriptLanguage::eNative)
        {
#ifdef _WIN32
            const auto invalidLibrary = binaries / "test-native-value-declarations-bad.dll";
#else
            const auto invalidLibrary = binaries / "libtest-native-value-declarations-bad.so";
#endif
            auto& invalid = world.addChild(world.root(), std::make_unique<Node3D>("Invalid declaration"));
            reject(
                [&]
                {
                    host.add({language, invalidLibrary, "ValueProbe", invalid.idInScene().value});
                },
                "Native declaration exception escaped its bootstrap");
            require(!world.registry().get<SceneProcess>(invalid.entity()).behavior,
                    "Malformed native declaration attached a behavior");
        }
        const auto& schema = host.scriptClass(first.id());
        require(schema.properties.size() == 13 && schema.property(field("samples")).integerBits == 16 &&
                    schema.property(field("modes")).elementKind == PropertyKind::eEnum &&
                    schema.property(field("target")).referenceType == "Node3D" &&
                    schema.property(field("material")).referenceCategory == ObjectCategory::eResource &&
                    host.readExport(first.id(), field("basis")) == PropertyValue(basis) &&
                    host.readExport(first.id(), field("uv")) == PropertyValue(glm::vec2 {.25f, .75f}) &&
                    host.readExport(first.id(), field("tint")) == PropertyValue(glm::vec4 {1, 0, 0, 1}),
                "Complex script schemas/defaults differ between languages");
        host.update(0.01f);
        require(first.localTransform()[3] == glm::vec4 {14, 14, 5, 1} &&
                    second.localTransform()[3] == glm::vec4 {4, 12, 4, 1} &&
                    host.readExport(first.id(), field("readyTarget")) == PropertyValue(std::string("Second")),
                "Enums, arrays, vectors, matrix or object references did not drive script logic");
        host.writeExport(first.id(), field("modes"), PropertyArray {{PropertyEnum {2}, PropertyEnum {0}}});
        host.update(0.01f);
        require(first.localTransform()[3].y == 16 && second.localTransform()[3].y == 12,
                "Live enum array did not reach its language instance");
        host.writeExport(first.id(), field("modes"), PropertyArray {{PropertyEnum {0}, PropertyEnum {2}}});
        reject(
            [&]
            {
                host.writeExport(first.id(), field("mode"), PropertyEnum {7});
            },
            "Unknown enum value was accepted",
            "Unknown enum value");
        reject(
            [&]
            {
                host.writeExport(first.id(), field("samples"), PropertyArray {{int64_t {1} << 30}});
            },
            "Array element width was not checked",
            "out of range");
        reject(
            [&]
            {
                host.writeExport(first.id(), field("modes"), PropertyArray {{PropertyEnum {7}}});
            },
            "Unknown enum array element was accepted",
            "Unknown enum value");
        reject(
            [&]
            {
                host.writeExport(first.id(), field("target"), PropertyReference {plain.idInScene().value});
            },
            "Wrong native node type was accepted",
            "live Node3D");
        reject(
            [&]
            {
                host.writeExport(first.id(), field("target"), PropertyReference {foreign.root().idInScene().value});
            },
            "Foreign world reference was accepted",
            "this world");
        host.writeExport(first.id(), field("samples"), PropertyArray {{int64_t {10}}});
        host.update(0.01f);
        require(first.localTransform()[3].x == 17 && second.localTransform()[3].x == 4,
                "Live array values alias another script instance");
        sceneWriteProperty(world, nullptr, first.id(), field("target"), "null");
        require(host.readExport(first.id(), field("target")) == PropertyValue(PropertyReference {}),
                "Nullable reference did not clear");
        host.update(0.01f);
        require(host.readExport(first.id(), field("readyTarget")) == PropertyValue(std::string("cleared")),
                "A nil exported reference exposed class data or a stale instance value");
        host.writeExport(first.id(), field("target"), PropertyReference {second.idInScene().value});
        std::ofstream(copy, std::ios::binary | std::ios::trunc) << "invalid replacement";
        std::filesystem::last_write_time(copy, stamp + std::chrono::seconds(2));
        require(host.reloadChanged() == 0 && !host.lastReloadError().empty(),
                "Broken complex-value replacement was accepted");
        host.update(0.01f);
        require(first.localTransform()[3].x == 17, "Failed replacement damaged live arrays/references");
        std::filesystem::copy_file(replacement, copy, std::filesystem::copy_options::overwrite_existing);
        std::filesystem::last_write_time(copy, stamp + std::chrono::seconds(4));
        require(host.reloadChanged() == 2 && host.lastReloadError().empty(), "Complex-value replacement failed");
        host.update(0.01f);
        require(first.id() == firstId && second.id() == secondId &&
                    first.localTransform()[3] == glm::vec4 {14, 14, 8, 1} &&
                    second.localTransform()[3] == glm::vec4 {4, 12, 7, 1} &&
                    host.readExport(first.id(), field("target")) ==
                        PropertyValue(PropertyReference {second.idInScene().value}),
                "Replacement did not restore initial arrays or rebind object references");
        // Runtime references cannot revive a successor with the same authored identity.
        const auto secondPersistent = second.idInScene();
        auto       retired          = world.remove(second);
        retired.reset();
        reject(
            [&]
            {
                host.readExport(first.id(), field("target"));
            },
            "Retired runtime reference remained usable");
        auto& successor = world.addChild(world.root(), std::make_unique<Node3D>("Successor", secondPersistent));
        reject(
            [&]
            {
                host.readExport(first.id(), field("target"));
            },
            "A successor revived the preceding runtime reference");
        require(successor.id() != secondId, "Successor reused runtime identity");
        host.writeExport(first.id(), field("mode"), PropertyEnum {0});
        host.writeExport(first.id(), field("target"), PropertyReference {});
        host.writeExport(first.id(), field("peers"), PropertyArray {});
        host.update(0.01f);
        host.stop();
    }
} // namespace

int main(int, char** argv)
try
{
    const auto directory =
        std::filesystem::absolute("build/.tmp") / ("script-values-" + StableId::generate().toString());
    std::filesystem::create_directories(directory);
    for (const auto language :
         {vultra::ScriptLanguage::eNative, vultra::ScriptLanguage::eLua, vultra::ScriptLanguage::eCSharp})
    {
        run(language, std::filesystem::absolute(argv[0]).parent_path(), directory);
    }
    std::cout
        << "Script values passed: enums, arrays, vectors/matrix, typed references, logic, reload and retirement\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
