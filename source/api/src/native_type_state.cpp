#include "native_type_state.hpp"

#include <vultra/core/base/logger.hpp>
#include <vultra/scene/scene_tree.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace vultra
{
    namespace
    {
        using Json = nlohmann::json;

        void check(VultraStatus status, const std::string& operation)
        {
            if (status != VULTRA_STATUS_OK)
            {
                if (status == VULTRA_STATUS_INVALID_ARGUMENT)
                {
                    throw std::invalid_argument(operation + ": status " + std::to_string(int(status)));
                }
                throw std::runtime_error(operation + ": status " + std::to_string(int(status)));
            }
        }

        std::string initialState(const NativeTypeRecord& type, std::string_view json)
        {
            const auto input = Json::parse(json);
            if (!input.is_object())
            {
                throw std::invalid_argument(type.name + ": extension data must be an object");
            }
            auto result = Json::object();
            for (const auto& field : type.fields)
            {
                const Json::json_pointer path(field.jsonPath);
                auto                     value =
                    input.contains(path) ? deserializeProperty(field, input.at(path).dump()) : field.defaultValue;
                field.validate(value);
                result[path] = Json::parse(serializeProperty(field, value));
            }
            const auto validateKeys = [&](auto&& self, const Json& provided, const Json& known) -> void
            {
                if (provided.is_object())
                {
                    if (!known.is_object())
                    {
                        throw std::invalid_argument(type.name + ": invalid extension field structure");
                    }
                    for (const auto& [key, value] : provided.items())
                    {
                        if (!known.contains(key))
                        {
                            throw std::invalid_argument(type.name + ": unknown extension field " + key);
                        }
                        self(self, value, known.at(key));
                    }
                }
                else if (provided.is_array())
                {
                    if (!known.is_array() || provided.size() > known.size())
                    {
                        throw std::invalid_argument(type.name + ": unknown extension array entries");
                    }
                    for (size_t index = 0; index < provided.size(); ++index)
                    {
                        self(self, provided.at(index), known.at(index));
                    }
                }
                else if (known.is_structured() || (known.is_null() && !provided.is_null()))
                {
                    throw std::invalid_argument(type.name + ": invalid extension field structure");
                }
            };
            validateKeys(validateKeys, input, result);
            return result.dump();
        }
    } // namespace

    NativeTypeState::NativeTypeState(NativeTypeRecord& type, std::string_view json)
    {
        const auto state    = initialState(type, json);
        void*      instance = nullptr;
        const auto status   = type.api.create(type.factory, state.data(), state.size(), &instance);
        if (status != VULTRA_STATUS_OK || !instance)
        {
            if (instance)
            {
                type.api.destroy(instance);
            }
            check(status, type.name + ": create extension instance");
            throw std::runtime_error(type.name + ": create extension instance returned no state");
        }
        m_Type     = &type;
        m_Instance = instance;
        ++type.lifetime.instances;
    }

    NativeTypeState::~NativeTypeState()
    {
        reset();
    }

    NativeTypeState::NativeTypeState(NativeTypeState&& other) noexcept :
        m_Type(std::exchange(other.m_Type, nullptr)),
        m_Instance(std::exchange(other.m_Instance, nullptr))
    {
    }

    NativeTypeState& NativeTypeState::operator=(NativeTypeState&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            m_Type     = std::exchange(other.m_Type, nullptr);
            m_Instance = std::exchange(other.m_Instance, nullptr);
            m_Access   = {};
        }
        return *this;
    }

    void NativeTypeState::reset() noexcept
    {
        if (m_Instance)
        {
            if (m_Type->api.destroy(m_Instance) != VULTRA_STATUS_OK)
            {
                Logger::app().error("Destroy extension instance '{}': callback failed", m_Type->name);
            }
            --m_Type->lifetime.instances;
        }
        m_Instance = nullptr;
        m_Type     = nullptr;
    }

    NativeTypeRecord& NativeTypeState::type() const
    {
        return *m_Type;
    }

    PropertyValue NativeTypeState::read(const PropertySchema& property) const
    {
        uint64_t size = 0;
        check(m_Type->api.read(m_Instance, property.name.data(), property.name.size(), nullptr, 0, &size),
              m_Type->name + ": read " + property.name);
        std::string json(size, '\0');
        check(m_Type->api.read(m_Instance, property.name.data(), property.name.size(), json.data(), json.size(), &size),
              m_Type->name + ": read " + property.name);
        if (size != json.size())
        {
            throw std::runtime_error(m_Type->name + ": unstable property read size");
        }
        return deserializeProperty(property, json);
    }

    void NativeTypeState::write(const PropertySchema& property, const PropertyValue& value)
    {
        const auto json = serializeProperty(property, value);
        check(m_Type->api.write(m_Instance, property.name.data(), property.name.size(), json.data(), json.size()),
              m_Type->name + ": write " + property.name);
    }

    std::string NativeTypeState::serialize() const
    {
        auto result = Json::object();
        for (const auto& field : m_Type->fields)
        {
            if (hasPropertyFlag(field.flags, PropertyFlags::eSerialize))
            {
                result[Json::json_pointer(field.jsonPath)] = Json::parse(serializeProperty(field, read(field)));
            }
        }
        return result.dump();
    }

    void NativeTypeState::notify(SceneTree& scene, ObjectId id, VultraSceneEvent event, double deltaSeconds)
    {
        if (!m_Type->api.on_scene)
        {
            return;
        }
        m_Access.scene = &scene;
        ++m_Access.serial;
        m_Access.active = true;
        VultraStatus status;
        try
        {
            status = m_Type->api.on_scene(m_Instance, {&m_Access, m_Access.serial}, id.value, event, deltaSeconds);
        }
        catch (...)
        {
            m_Access.active = false;
            throw;
        }
        m_Access.active = false;
        if (status != VULTRA_STATUS_OK)
        {
            check(status, m_Type->name + ": lifecycle event " + std::to_string(int(event)));
        }
    }

    entt::registry::storage_for_type<NativeTypeState>& nativeTypeStorage(entt::registry& registry, entt::id_type id)
    {
        if (const auto* existing = registry.storage(id);
            existing && existing->type() != entt::type_id<NativeTypeState>())
        {
            throw std::invalid_argument("Extension component storage ID collides with another native component");
        }
        return registry.storage<NativeTypeState>(id);
    }

    const NativeTypeState* nativeTypeState(const entt::registry& registry, entt::entity entity, entt::id_type id)
    {
        if (const auto* existing = registry.storage(id);
            existing && existing->type() != entt::type_id<NativeTypeState>())
        {
            throw std::invalid_argument("Extension component storage ID collides with another native component");
        }
        const auto* storage = registry.storage<NativeTypeState>(id);
        return storage && storage->contains(entity) ? &storage->get(entity) : nullptr;
    }
} // namespace vultra
