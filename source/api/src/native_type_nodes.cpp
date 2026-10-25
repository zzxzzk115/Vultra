#include "native_type_state.hpp"

#include <vultra/scene/render_nodes.hpp>
#include <vultra/scene/scene_tree.hpp>

#include <nlohmann/json.hpp>

#include <optional>
#include <stdexcept>
#include <type_traits>

namespace vultra
{
    namespace
    {
        using Json = nlohmann::json;

        template<class Base>
        class ExtensionNode final : public Base
        {
        public:
            template<class... Arguments>
            ExtensionNode(NativeTypeRecord& type, std::string_view state, Arguments&&... arguments) :
                Base(std::forward<Arguments>(arguments)...),
                m_Type(type),
                m_Detached(std::in_place, type, state)
            {
            }

            std::string_view typeName() const override
            {
                return m_Type.name;
            }

            NativeTypeState& state() const
            {
                return this->tree() ? nativeTypeStorage(this->tree()->registry(), m_Type.storage).get(this->entity()) :
                                      *m_Detached;
            }

            void write(const PropertySchema& property, const PropertyValue& value)
            {
                state().write(property, value);
                if (this->tree())
                {
                    nativeTypeStorage(this->tree()->registry(), m_Type.storage).patch(this->entity());
                }
                this->markChanged(SceneChange::eMetadata);
            }

            void onEnterTree() override
            {
                Base::onEnterTree();
                state().notify(*this->tree(), this->id(), VULTRA_SCENE_ENTER_TREE);
            }

            void onReady() override
            {
                Base::onReady();
                state().notify(*this->tree(), this->id(), VULTRA_SCENE_READY);
            }

            void onPhysicsProcess(double delta) override
            {
                Base::onPhysicsProcess(delta);
                state().notify(*this->tree(), this->id(), VULTRA_SCENE_PHYSICS_PROCESS, delta);
            }

            void onProcess(double delta) override
            {
                Base::onProcess(delta);
                state().notify(*this->tree(), this->id(), VULTRA_SCENE_PROCESS, delta);
            }

            void onExitTree() override
            {
                state().notify(*this->tree(), this->id(), VULTRA_SCENE_EXIT_TREE);
                Base::onExitTree();
            }

        protected:
            void attachComponents(entt::registry& registry, entt::entity entity) override
            {
                auto& storage = nativeTypeStorage(registry, m_Type.storage);
                storage.emplace(entity, std::move(*m_Detached));
                m_Detached.reset();
                Base::attachComponents(registry, entity);
            }

            void detachComponents(entt::registry& registry, entt::entity entity) override
            {
                auto& storage = nativeTypeStorage(registry, m_Type.storage);
                if (storage.contains(entity))
                {
                    m_Detached.emplace(std::move(storage.get(entity)));
                    storage.remove(entity);
                }
                Base::detachComponents(registry, entity);
            }

        private:
            NativeTypeRecord&                      m_Type;
            mutable std::optional<NativeTypeState> m_Detached;
        };

        template<class Base>
        void bindNode(NativeTypeRecord& record)
        {
            using Proxy      = ExtensionNode<Base>;
            const auto& root = record.nodeBase->type;
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
            record.node.create = [&record](std::string name, NodeId id, std::string_view json) -> std::unique_ptr<Node>
            {
                auto       data  = Json::parse(json);
                const auto state = data.value("extension", Json::object()).dump();
                data.erase("extension");
                auto        base  = record.nodeBase->create(name, id, data.dump());
                const auto& typed = dynamic_cast<const Base&>(*base);
                auto        proxy = [&]
                {
                    if constexpr (std::is_same_v<Base, MeshInstance3D>)
                    {
                        return std::make_unique<Proxy>(record, state, name, typed.model(), id);
                    }
                    else if constexpr (std::is_same_v<Base, Light3D>)
                    {
                        return std::make_unique<Proxy>(record, state, name, typed.lightKind(), id);
                    }
                    else
                    {
                        return std::make_unique<Proxy>(record, state, name, id);
                    }
                }();
                for (const auto& property : record.nodeBase->type.properties)
                {
                    property.write(static_cast<Base*>(proxy.get()), property.read(&typed));
                }
                if constexpr (std::is_same_v<Base, MeshInstance3D>)
                {
                    for (const auto& material : typed.materialOverrides())
                    {
                        proxy->setMaterial(material.slot, material.material);
                    }
                }
                else if constexpr (std::is_same_v<Base, Camera3D> || std::is_same_v<Base, Light3D> ||
                                   std::is_same_v<Base, WorldEnvironment>)
                {
                    // Base codecs also persist settings held by components, outside flattened Node properties.
                    proxy->setSettings(typed.settings());
                }
                return proxy;
            };
            record.node.serialize = [&record](const Node& node)
            {
                const auto& proxy = dynamic_cast<const Proxy&>(node);
                auto        data  = Json::parse(record.nodeBase->serialize(proxy));
                data["extension"] = Json::parse(proxy.state().serialize());
                return data.dump();
            };
            record.node.lifetime      = &record.lifetime;
            record.node.extensionBase = &root;
        }
    } // namespace

    void bindNativeNode(NativeTypeRecord& record)
    {
        const std::string_view root = record.nodeBase->type.name;
        if (root == "Node")
        {
            bindNode<Node>(record);
        }
        else if (root == "Node3D")
        {
            bindNode<Node3D>(record);
        }
        else if (root == "MeshInstance3D")
        {
            bindNode<MeshInstance3D>(record);
        }
        else if (root == "Camera3D")
        {
            bindNode<Camera3D>(record);
        }
        else if (root == "WorldEnvironment")
        {
            bindNode<WorldEnvironment>(record);
        }
        else if (root == "DirectionalLight3D" || root == "OmniLight3D" || root == "SpotLight3D")
        {
            bindNode<Light3D>(record);
        }
        else
        {
            throw std::invalid_argument("Unsupported native extension node base: " + std::string(root));
        }
    }
} // namespace vultra
