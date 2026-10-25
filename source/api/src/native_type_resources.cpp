#include "native_type_state.hpp"

#include <vultra/scene/material_resource.hpp>

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <type_traits>

namespace vultra
{
    namespace
    {
        using Json = nlohmann::json;

        template<class Base>
        class ExtensionResource final : public Base
        {
        public:
            template<class... Arguments>
            ExtensionResource(NativeTypeRecord& type, std::string_view state, Arguments&&... arguments) :
                Base(std::forward<Arguments>(arguments)...),
                m_Type(type),
                m_State(type, state)
            {
            }

            std::string_view typeName() const override
            {
                return m_Type.name;
            }

            const NativeTypeState& state() const
            {
                return m_State;
            }

            void write(const PropertySchema& property, const PropertyValue& value)
            {
                m_State.write(property, value);
                this->markChanged(SceneChange::eMetadata);
            }

        private:
            NativeTypeRecord& m_Type;
            NativeTypeState   m_State;
        };

        template<class Base>
        void bindResource(NativeTypeRecord& record)
        {
            using Proxy      = ExtensionResource<Base>;
            const auto& root = record.resourceBase->type;
            for (const auto& field : root.properties)
            {
                auto property = field;
                property.get  = [field](const void* object)
                {
                    return field.read(static_cast<const Base*>(static_cast<const Proxy*>(object)));
                };
                property.set = [field](void* object, const PropertyValue& value)
                {
                    field.write(static_cast<Base*>(static_cast<Proxy*>(object)), value);
                };
                record.properties.push_back(std::move(property));
            }
            for (const auto& field : record.fields)
            {
                PropertyInfo property;
                static_cast<PropertySchema&>(property) = field;
                property.jsonPath                      = "/extension" + field.jsonPath;
                property.get                           = [field](const void* object)
                {
                    return static_cast<const Proxy*>(object)->state().read(field);
                };
                property.set = [field](void* object, const PropertyValue& value)
                {
                    static_cast<Proxy*>(object)->write(field, value);
                };
                record.properties.push_back(std::move(property));
            }
            record.resource.create = [&record](ResourceId id, std::string_view json) -> std::unique_ptr<Resource>
            {
                auto       data  = Json::parse(json);
                const auto state = data.value("extension", Json::object()).dump();
                data.erase("extension");
                auto        base  = record.resourceBase->create(id, data.dump());
                const auto& typed = dynamic_cast<const Base&>(*base);
                auto        proxy = [&]
                {
                    if constexpr (std::is_same_v<Base, MaterialResource>)
                    {
                        return std::make_unique<Proxy>(record, state, typed.name(), id);
                    }
                    else
                    {
                        return std::make_unique<Proxy>(record, state, id);
                    }
                }();
                for (const auto& property : record.resourceBase->type.properties)
                {
                    property.write(static_cast<Base*>(proxy.get()), property.read(&typed));
                }
                if constexpr (std::is_same_v<Base, MaterialResource>)
                {
                    if (typed.kind() == MaterialResource::Kind::eOpenPbr)
                    {
                        proxy->setParameters(typed.parameters());
                    }
                    else
                    {
                        proxy->setShaderMaterial(typed.shaderMaterial());
                    }
                }
                return proxy;
            };
            record.resource.serialize = [&record](const Resource& resource)
            {
                const auto& proxy = dynamic_cast<const Proxy&>(resource);
                auto        data  = Json::parse(record.resourceBase->serialize(proxy));
                data["extension"] = Json::parse(proxy.state().serialize());
                return data.dump();
            };
            record.resource.lifetime      = &record.lifetime;
            record.resource.extensionBase = &root;
        }
    } // namespace

    void bindNativeResource(NativeTypeRecord& record)
    {
        const std::string_view root = record.resourceBase->type.name;
        if (root == "Resource")
        {
            bindResource<Resource>(record);
        }
        else if (root == "MaterialResource")
        {
            bindResource<MaterialResource>(record);
        }
        else
        {
            throw std::invalid_argument("Unsupported native extension resource base: " + std::string(root));
        }
    }
} // namespace vultra
