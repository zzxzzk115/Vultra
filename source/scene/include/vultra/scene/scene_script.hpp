#pragma once

#include <vultra/assets/asset_id.hpp>

#include <string>

namespace vultra
{
    // Authored configuration only. The optional host owns code, schemas and live language instances.
    struct SceneScript
    {
        AssetId     module;
        std::string className;
        // JSON object decoded against the current script schema when attaching/replacing an instance.
        std::string exports = "{}";

        void validate() const;
    };
} // namespace vultra
