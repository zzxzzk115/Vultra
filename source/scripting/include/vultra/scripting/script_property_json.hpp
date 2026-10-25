#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace vultra::scripting
{
    inline nlohmann::json scriptPropertyDescription(std::string_view name,
                                                    std::string_view kind,
                                                    uint8_t          integerBits,
                                                    nlohmann::json   defaultValue)
    {
        std::string path = "/";
        for (const auto character : name)
        {
            if (character == '~')
            {
                path += "~0";
            }
            else if (character == '/')
            {
                path += "~1";
            }
            else
            {
                path += character;
            }
        }
        return {{"name", name},
                {"label", name},
                {"drawer", nullptr},
                {"kind", kind},
                {"json_path", path},
                {"default", std::move(defaultValue)},
                {"flags", {"serialize", "inspect", "bind"}},
                {"widget", "drag"},
                {"speed", 0.1},
                {"integer_bits", integerBits},
                {"choices", nlohmann::json::array()},
                {"enum_values", nlohmann::json::array()}};
    }
} // namespace vultra::scripting
