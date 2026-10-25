#include <vultra/scene/scene_api.hpp>
#include <vultra/scene/scene_tree.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>

namespace
{
    using namespace vultra;
    using Json = nlohmann::json;

    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    template<class Operation>
    void reject(Operation operation)
    {
        try
        {
            operation();
        }
        catch (const std::exception&)
        {
            return;
        }
        throw std::runtime_error("Invalid world random operation was accepted");
    }
} // namespace

int main()
try
{
    SceneTree first(std::make_unique<Node>("First"));
    SceneTree second(std::make_unique<Node>("Second"));
    first.setRandomSeed(123);
    second.setRandomSeed(123);
    for (int i = 0; i < 20; ++i)
    {
        require(first.randomUInt("movement") == second.randomUInt("movement"), "Seeded worlds disagree");
        first.randomUInt("unrelated");
    }
    const auto             authored = first.serialize();
    const auto             saved    = first.randomState();
    std::array<double, 20> units {};
    for (auto& unit : units)
    {
        unit = first.randomUnit("movement");
    }
    first.restoreRandomState(saved);
    for (const auto unit : units)
    {
        require(first.randomUnit("movement") == unit, "World random checkpoint did not replay exactly");
    }
    require(first.serialize() == authored, "Transient random draws changed the authored scene document");
    auto restored = SceneTree::parse(authored);
    restored.restoreRandomState(saved);
    SceneTree moved(std::move(restored));
    require(moved.randomState() == saved && moved.randomUnit("movement") == units.front(),
            "Restoring a world or moving it lost the named stream state");
    const auto before                  = first.randomState();
    auto       invalid                 = Json::parse(before);
    invalid["streams"][0]["increment"] = 2;
    reject(
        [&]
        {
            first.restoreRandomState(invalid.dump());
        });
    invalid = Json::parse(before);
    invalid["streams"].push_back(invalid["streams"][0]);
    reject(
        [&]
        {
            first.restoreRandomState(invalid.dump());
        });
    invalid         = Json::parse(before);
    invalid["seed"] = -1;
    reject(
        [&]
        {
            first.restoreRandomState(invalid.dump());
        });
    invalid            = Json::parse(before);
    invalid["version"] = 2;
    reject(
        [&]
        {
            first.restoreRandomState(invalid.dump());
        });
    reject(
        [&]
        {
            sceneSetRandomSeed(first, uint64_t(std::numeric_limits<uint32_t>::max()) + 1);
        });
    reject(
        [&]
        {
            sceneRandomBoundedUInt(first, "new", uint64_t(std::numeric_limits<uint32_t>::max()) + 1);
        });
    reject(
        [&]
        {
            first.randomUInt("new", 0);
        });
    reject(
        [&]
        {
            first.randomUInt("");
        });
    reject(
        [&]
        {
            first.randomUInt(std::string_view("\xff", 1));
        });
    require(first.randomState() == before, "Rejected checkpoint/arguments changed live random state");
    first.start();
    reject(
        [&]
        {
            first.setRandomSeed(9);
        });
    reject(
        [&]
        {
            first.restoreRandomState(saved);
        });
    first.stop();
    require(first.randomState() == before, "Stop/start or rejected live reset changed stream state");
    first.setRandomSeed(123);
    second.setRandomSeed(123);
    require(first.randomState() == second.randomState() &&
                first.randomUInt("movement") == second.randomUInt("movement"),
            "Explicit reseeding did not reset all world-owned streams");
    std::cout
        << "World random streams passed: independent names/worlds, checkpoint replay, failure recovery and lifecycle\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
