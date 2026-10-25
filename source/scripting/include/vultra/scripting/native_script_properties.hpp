#pragma once

#include <vultra/api/vultra_scene.generated.h>
#include <vultra/scripting/script_property_json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace vultra::scripting
{
    class NativeScript;

    struct Float2
    {
        float x = 0;
        float y = 0;
    };

    struct Float3
    {
        float x = 0;
        float y = 0;
        float z = 0;
    };

    struct Float4
    {
        float x = 0;
        float y = 0;
        float z = 0;
        float w = 0;
    };

    struct Float4x4
    {
        // Same column-major value representation as core PropertyKind::eMatrix4; no binary layout crosses the ABI.
        std::array<float, 16> columns {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    };

    // Owned runtime references contain no pointer to the script that created them. Never persist these IDs.
    struct NodeReference
    {
        uint64_t    id                                       = 0;
        friend bool operator==(NodeReference, NodeReference) = default;
    };

    struct ResourceReference
    {
        uint64_t    id                                               = 0;
        friend bool operator==(ResourceReference, ResourceReference) = default;
    };

    struct NativeEnumChoice
    {
        std::string label;
        int64_t     value;

        template<class Enum>
            requires std::is_enum_v<Enum>
        NativeEnumChoice(std::string_view label, Enum value) :
            label(label),
            value(static_cast<int64_t>(value))
        {
            static_assert(sizeof(std::underlying_type_t<Enum>) <= sizeof(int64_t),
                          "Exported enums are at most 64 bits");
            if constexpr (std::is_unsigned_v<std::underlying_type_t<Enum>>)
            {
                if (uint64_t(value) > uint64_t(INT64_MAX))
                {
                    throw std::invalid_argument("Exported enums require signed 64-bit values");
                }
            }
        }
    };

    struct NativePropertyOptions
    {
        std::vector<NativeEnumChoice> choices;
        std::string                   referenceType;
        bool                          nullable = true;
        std::string                   referenceClass {};
        std::string                   referenceModule {};
    };

    struct NativeScriptProperty
    {
        std::string                                                                                        name;
        nlohmann::json                                                                                     schema;
        std::function<nlohmann::json(const NativeScript&, const VultraSceneApi&, VultraSceneFrame)>        read;
        std::function<void(NativeScript&, const VultraSceneApi&, VultraSceneFrame, const nlohmann::json&)> write;
    };

    namespace detail
    {
        template<class Value>
        struct NativeArrayElement
        {
            using Type                   = Value;
            static constexpr bool kArray = false;
        };

        template<class Element, class Allocator>
        struct NativeArrayElement<std::vector<Element, Allocator>>
        {
            using Type                   = Element;
            static constexpr bool kArray = true;
        };

        inline void checkPropertyStatus(VultraStatus status, const char* operation)
        {
            if (status != VULTRA_STATUS_OK)
            {
                throw std::runtime_error(std::string(operation) + ": API status " + std::to_string(int(status)));
            }
        }

        inline nlohmann::json referenceJson(const VultraSceneApi& api, VultraSceneFrame frame, uint64_t id, bool node)
        {
            if (id == 0)
            {
                return nullptr;
            }
            uint64_t               size = 0;
            const std::string_view type = node ? "Node" : "Resource";
            checkPropertyStatus(api.object_persistent_id(frame, id, type.data(), type.size(), nullptr, 0, &size),
                                "Read reference identity");
            std::string value(size, '\0');
            checkPropertyStatus(
                api.object_persistent_id(frame, id, type.data(), type.size(), value.data(), value.size(), &size),
                "Read reference identity");
            return value;
        }

        template<class Value>
        std::pair<std::string_view, uint8_t> propertyKind()
        {
            if constexpr (std::is_same_v<Value, bool>)
            {
                return {"bool", 0};
            }
            else if constexpr (std::is_integral_v<Value>)
            {
                return {std::is_signed_v<Value> ? "int" : "uint", sizeof(Value) * 8};
            }
            else if constexpr (std::is_same_v<Value, float> || std::is_same_v<Value, double>)
            {
                return {std::is_same_v<Value, float> ? "float" : "double", 0};
            }
            else if constexpr (std::is_enum_v<Value>)
            {
                return {"enum", 0};
            }
            else if constexpr (std::is_same_v<Value, Float2>)
            {
                return {"vector2", 0};
            }
            else if constexpr (std::is_same_v<Value, Float3>)
            {
                return {"vector3", 0};
            }
            else if constexpr (std::is_same_v<Value, Float4>)
            {
                return {"vector4", 0};
            }
            else if constexpr (std::is_same_v<Value, Float4x4>)
            {
                return {"matrix4", 0};
            }
            else if constexpr (std::is_same_v<Value, NodeReference> || std::is_same_v<Value, ResourceReference>)
            {
                return {"reference", 0};
            }
            else
            {
                static_assert(std::is_same_v<Value, std::string>, "Unsupported native script export type");
                return {"string", 0};
            }
        }

        template<class Value>
        nlohmann::json readExport(const Value& value, const VultraSceneApi& api, VultraSceneFrame frame)
        {
            if constexpr (NativeArrayElement<Value>::kArray)
            {
                using Element = typename NativeArrayElement<Value>::Type;
                auto result   = nlohmann::json::array();
                for (const Element& item : value)
                {
                    result.push_back(readExport(item, api, frame));
                }
                return result;
            }
            else if constexpr (std::is_same_v<Value, NodeReference> || std::is_same_v<Value, ResourceReference>)
            {
                return referenceJson(api, frame, value.id, std::is_same_v<Value, NodeReference>);
            }
            else if constexpr (std::is_enum_v<Value>)
            {
                return static_cast<int64_t>(value);
            }
            else if constexpr (std::is_same_v<Value, Float2>)
            {
                return nlohmann::json::array({value.x, value.y});
            }
            else if constexpr (std::is_same_v<Value, Float3>)
            {
                return nlohmann::json::array({value.x, value.y, value.z});
            }
            else if constexpr (std::is_same_v<Value, Float4>)
            {
                return nlohmann::json::array({value.x, value.y, value.z, value.w});
            }
            else if constexpr (std::is_same_v<Value, Float4x4>)
            {
                return value.columns;
            }
            else
            {
                return value;
            }
        }

        template<class Value>
        Value writeExport(const nlohmann::json& value, const VultraSceneApi& api, VultraSceneFrame frame)
        {
            if constexpr (NativeArrayElement<Value>::kArray)
            {
                if (!value.is_array())
                {
                    throw std::invalid_argument("Native export requires an array");
                }
                Value result;
                result.reserve(value.size());
                for (const auto& item : value)
                {
                    result.push_back(writeExport<typename NativeArrayElement<Value>::Type>(item, api, frame));
                }
                return result;
            }
            else if constexpr (std::is_same_v<Value, NodeReference> || std::is_same_v<Value, ResourceReference>)
            {
                if (value.is_null())
                {
                    return {};
                }
                const auto id     = value.get<std::string>();
                const auto find   = std::is_same_v<Value, NodeReference> ? api.find_node : api.find_resource;
                uint64_t   target = 0;
                checkPropertyStatus(find(frame, id.data(), id.size(), &target), "Resolve reference identity");
                if (target == 0)
                {
                    throw std::invalid_argument("Reference target is not loaded in this world");
                }
                return {target};
            }
            else if constexpr (std::is_enum_v<Value>)
            {
                return static_cast<Value>(value.get<int64_t>());
            }
            else if constexpr (std::is_same_v<Value, Float2>)
            {
                return {value.at(0).get<float>(), value.at(1).get<float>()};
            }
            else if constexpr (std::is_same_v<Value, Float3>)
            {
                return {value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>()};
            }
            else if constexpr (std::is_same_v<Value, Float4>)
            {
                return {value.at(0).get<float>(),
                        value.at(1).get<float>(),
                        value.at(2).get<float>(),
                        value.at(3).get<float>()};
            }
            else if constexpr (std::is_same_v<Value, Float4x4>)
            {
                return {value.get<std::array<float, 16>>()};
            }
            else
            {
                return value.get<Value>();
            }
        }
    } // namespace detail

    template<class Owner, class Value>
    NativeScriptProperty
    scriptProperty(std::string_view name, Value Owner::* member, NativePropertyOptions options = {})
    {
        static_assert(!std::is_const_v<Value>, "Exported fields must be writable");
        using Element = typename detail::NativeArrayElement<Value>::Type;
        static_assert(!detail::NativeArrayElement<Element>::kArray, "Nested script arrays are not supported");
        const auto [kind, bits] = detail::propertyKind<Element>();
        NativeScriptProperty property;
        property.name = name;
        property.schema =
            scriptPropertyDescription(name, detail::NativeArrayElement<Value>::kArray ? "array" : kind, bits, nullptr);
        if constexpr (detail::NativeArrayElement<Value>::kArray)
        {
            property.schema["element_kind"] = kind;
        }
        if constexpr (std::is_enum_v<Element>)
        {
            if (options.choices.empty())
            {
                throw std::invalid_argument("Native enum exports require explicit choices");
            }
            for (const auto& choice : options.choices)
            {
                using Underlying = std::underlying_type_t<Element>;
                if constexpr (std::is_signed_v<Underlying>)
                {
                    if (choice.value < int64_t(std::numeric_limits<Underlying>::min()) ||
                        choice.value > int64_t(std::numeric_limits<Underlying>::max()))
                    {
                        throw std::invalid_argument("Enum choice exceeds its native underlying type");
                    }
                }
                else if (choice.value < 0 || uint64_t(choice.value) > uint64_t(std::numeric_limits<Underlying>::max()))
                {
                    throw std::invalid_argument("Enum choice exceeds its native underlying type");
                }
                property.schema["choices"].push_back({{"label", choice.label}, {"value", choice.value}});
                auto& values = property.schema["enum_values"];
                if (std::find(values.begin(), values.end(), choice.value) == values.end())
                {
                    values.push_back(choice.value);
                }
            }
        }
        else if (!options.choices.empty())
        {
            throw std::invalid_argument("Only enum exports accept choices");
        }
        if constexpr (std::is_same_v<Element, NodeReference> || std::is_same_v<Element, ResourceReference>)
        {
            const bool  node = std::is_same_v<Element, NodeReference>;
            std::string type = options.referenceType;
            if (type.empty())
            {
                type = node ? "Node" : "Resource";
            }
            property.schema["reference"] = {{"category", node ? "node" : "resource"},
                                            {"type", type},
                                            {"nullable", options.nullable}};
            if (!options.referenceClass.empty())
            {
                if (!node)
                {
                    throw std::invalid_argument("Script class references require a Node field");
                }
                property.schema["reference"]["class"] = {{"module", options.referenceModule},
                                                         {"name", options.referenceClass}};
            }
            else if (!options.referenceModule.empty())
            {
                throw std::invalid_argument("Reference module requires a script class name");
            }
        }
        else if (!options.referenceType.empty() || !options.nullable || !options.referenceClass.empty() ||
                 !options.referenceModule.empty())
        {
            throw std::invalid_argument("Only reference exports accept a target type/nullability");
        }
        property.read =
            [member](const NativeScript& script, const VultraSceneApi& api, VultraSceneFrame frame) -> nlohmann::json
        {
            return detail::readExport(dynamic_cast<const Owner&>(script).*member, api, frame);
        };
        property.write = [member](NativeScript&         script,
                                  const VultraSceneApi& api,
                                  VultraSceneFrame      frame,
                                  const nlohmann::json& value)
        {
            dynamic_cast<Owner&>(script).*member = detail::writeExport<Value>(value, api, frame);
        };
        return property;
    }
} // namespace vultra::scripting
