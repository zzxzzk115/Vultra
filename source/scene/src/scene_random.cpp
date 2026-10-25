#include <vultra/scene/scene_tree.hpp>

#include <nlohmann/json.hpp>

#include <limits>
#include <stdexcept>

namespace vultra
{
    namespace
    {
        void validateName(std::string_view name)
        {
            if (name.empty() || name.find('\0') != std::string_view::npos)
            {
                throw std::invalid_argument("Random stream requires a nonempty name without NUL");
            }
        }

        uint64_t readWord(const nlohmann::json& value)
        {
            if (!value.is_number_unsigned())
            {
                throw std::invalid_argument("Random state words must be unsigned 64-bit integers");
            }
            return value.get<uint64_t>();
        }
    } // namespace

    uint32_t SceneTree::randomSeed() const
    {
        return m_RandomSeed;
    }

    void SceneTree::setRandomSeed(uint32_t seed)
    {
        requireIdle();
        if (running())
        {
            throw std::logic_error("Reset random streams in a stopped world");
        }
        m_RandomSeed = seed;
        m_RandomStreams.clear();
    }

    RandomStream& SceneTree::randomStream(std::string_view name)
    {
        auto found = m_RandomStreams.find(name);
        if (found == m_RandomStreams.end())
        {
            validateName(name);
            // Reject an invalid persisted key before inserting or consuming a stream; validate only once.
            (void)nlohmann::json(name).dump();
            // Stable UTF-8 byte hashing; std::hash is not a replay/file-format contract.
            uint64_t sequence = 14695981039346656037ULL;
            for (const unsigned char byte : name)
            {
                sequence = (sequence ^ byte) * 1099511628211ULL;
            }
            found = m_RandomStreams.try_emplace(std::string(name), m_RandomSeed, sequence).first;
        }
        return found->second;
    }

    uint32_t SceneTree::randomUInt(std::string_view stream)
    {
        return randomStream(stream).nextUInt();
    }

    uint32_t SceneTree::randomUInt(std::string_view stream, uint32_t bound)
    {
        if (bound == 0)
        {
            throw std::invalid_argument("Random integer bound must be positive");
        }
        return randomStream(stream).nextUInt(bound);
    }

    double SceneTree::randomUnit(std::string_view stream)
    {
        return randomStream(stream).nextUnit();
    }

    std::string SceneTree::randomState() const
    {
        using Json = nlohmann::json;
        Json value {{"format", "vultra.random"},
                    {"version", 1},
                    {"algorithm", "pcg32"},
                    {"seed", m_RandomSeed},
                    {"streams", Json::array()}};
        for (const auto& [name, stream] : m_RandomStreams)
        {
            const auto state = stream.state();
            value["streams"].push_back({{"name", name}, {"state", state.state}, {"increment", state.increment}});
        }
        return value.dump();
    }

    void SceneTree::restoreRandomState(std::string_view json)
    {
        requireIdle();
        if (running())
        {
            throw std::logic_error("Restore random streams in a stopped world");
        }
        using Json       = nlohmann::json;
        const auto value = Json::parse(json);
        if (value.at("format") != "vultra.random" || value.at("version") != 1 || value.at("algorithm") != "pcg32")
        {
            throw std::invalid_argument("Expected vultra.random version 1 with PCG32 state");
        }
        const auto seed = readWord(value.at("seed"));
        if (seed > std::numeric_limits<uint32_t>::max() || !value.at("streams").is_array())
        {
            throw std::invalid_argument("Random state requires a 32-bit seed and a stream array");
        }
        std::map<std::string, RandomStream, std::less<>> streams;
        for (const auto& entry : value.at("streams"))
        {
            const auto name = entry.at("name").get<std::string>();
            validateName(name);
            RandomStream stream;
            stream.restore({readWord(entry.at("state")), readWord(entry.at("increment"))});
            if (!streams.emplace(name, stream).second)
            {
                throw std::invalid_argument("Duplicate random stream name: " + name);
            }
        }
        m_RandomStreams = std::move(streams);
        m_RandomSeed    = uint32_t(seed);
    }
} // namespace vultra
