#pragma once

#include <vultra/core/base/property.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace vultra
{
    struct ScriptClassInfo
    {
        std::string                  name;
        std::string                  baseType;
        std::vector<PropertySchema>  properties;
        std::vector<PropertyClassId> classes;
        std::vector<std::string>     dependencies;
        bool                         input          = false;
        bool                         unhandledInput = false;

        const PropertySchema& property(std::string_view name) const;
    };

    struct ScriptExport
    {
        std::string   name;
        PropertyValue value;
    };
} // namespace vultra
