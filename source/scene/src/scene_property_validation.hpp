#pragma once

#include <vultra/core/base/property.hpp>

namespace vultra::detail
{
    // Generic schemas accept application-defined categories; a world only resolves nodes and resources.
    void validateSceneProperty(const PropertySchema& property);
} // namespace vultra::detail
