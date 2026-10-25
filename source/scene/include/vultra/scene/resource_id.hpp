#pragma once

#include <vultra/core/base/api_annotations.hpp>
#include <vultra/core/base/stable_id.hpp>

namespace vultra
{
    // Persistent identity in a scene document. This is neither a project asset key nor a runtime ObjectId.
    struct VULTRA_REFERENCE("resource") ResourceId
    {
        StableId    value;
        friend bool operator==(const ResourceId&, const ResourceId&) = default;
    };
} // namespace vultra
