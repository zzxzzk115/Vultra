#include "native_type_state.hpp"

#include <vultra/api/native_extension.hpp>
#include <vultra/api/native_plugin.hpp>
#include <vultra/api/native_type_api.hpp>
#include <vultra/core/base/logger.hpp>

#include <entt/core/hashed_string.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <exception>
#include <stdexcept>

namespace vultra
{
    static_assert(sizeof(NativeTypeApi) == sizeof(VultraNativeTypeApi));
    static_assert(sizeof(TypeRegistrationApi) == sizeof(VultraTypeRegistrationApi));

    struct NativeExtension::Impl
    {
        SceneTypeCatalog&                              types;
        std::vector<std::unique_ptr<NativeTypeRecord>> records;
        std::unique_ptr<NativePlugin>                  plugin;
        std::unique_ptr<PluginSession>                 session;
        VultraTypeRegistrationApi                      registration {};
        VultraExtensionApi                             domain {};
        bool                                           registering = true;

        explicit Impl(SceneTypeCatalog& types) :
            types(types)
        {
        }

        void add(std::string_view json, const VultraNativeTypeApi& api, void* factory)
        {
            using Json = nlohmann::json;
            if (!registering)
            {
                throw std::invalid_argument("Type registration has ended");
            }
            if (api.version != VULTRA_ABI_VERSION || api.struct_size != sizeof(VultraNativeTypeApi) || !api.create ||
                !api.destroy)
            {
                throw std::invalid_argument(
                    "Native type requires matching ABI, construction and destruction callbacks");
            }
            const auto data = Json::parse(json);
            if (!data.at("version").is_number_integer() || data.at("version") != 1 || !data.at("properties").is_array())
            {
                throw std::invalid_argument("Native type requires version-1 property metadata");
            }
            auto record  = std::make_unique<NativeTypeRecord>();
            record->name = data.at("name").get<std::string>();
            if (record->name.empty() || record->name.find('\0') != std::string::npos)
            {
                throw std::invalid_argument("Native type requires a nonempty name without NUL bytes");
            }
            record->api           = api;
            record->factory       = factory;
            const auto category   = data.at("category").get<std::string>();
            record->type.category = category == "node" ? "node" : "value";
            if (category == "resource")
            {
                record->type.category = "resource";
            }
            else if (category == "component")
            {
                record->type.category = "component";
            }
            else if (category != "node")
            {
                throw std::invalid_argument("Native type category must be node, resource or component");
            }
            if (record->type.category != "node" && api.on_scene)
            {
                throw std::invalid_argument("Only node types have scene lifecycle callbacks");
            }
            if (!data.at("base").is_null())
            {
                record->type.base = &types.objects().type(data.at("base").get<std::string>());
            }
            const auto* base = record->type.base;
            if (record->type.category == "node")
            {
                if (!base || base->category != "node")
                {
                    throw std::invalid_argument("Extension node requires a registered node base");
                }
                const auto& parent = types.nodeType(base->name);
                record->nodeBase   = &types.nodeType(parent.extensionBase ? parent.extensionBase->name : base->name);
            }
            else if (record->type.category == "resource")
            {
                if (!base || base->category != "resource")
                {
                    throw std::invalid_argument("Extension resource requires a registered resource base");
                }
                const auto& parent = types.resourceType(base->name);
                record->resourceBase =
                    &types.resourceType(parent.extensionBase ? parent.extensionBase->name : base->name);
            }
            else if (base)
            {
                throw std::invalid_argument("Extension components do not inherit storage");
            }
            if (base && (base != &(record->nodeBase ? record->nodeBase->type : record->resourceBase->type)))
            {
                for (const auto& property : base->properties)
                {
                    if (property.jsonPath.starts_with("/extension/"))
                    {
                        record->fields.push_back(property);
                        record->fields.back().jsonPath.erase(0, std::string_view("/extension").size());
                    }
                }
            }
            for (const auto& property : data.at("properties"))
            {
                record->fields.push_back(parsePropertySchema(Json {{"version", 1}, {"property", property}}.dump()));
            }
            if (!record->fields.empty() && (!api.read || !api.write))
            {
                throw std::invalid_argument("Native properties require read/write callbacks");
            }
            for (const auto& field : record->fields)
            {
                if (field.kind == PropertyKind::eReference ||
                    (field.kind == PropertyKind::eArray && field.elementKind == PropertyKind::eReference))
                {
                    if (field.referenceCategory == "asset")
                    {
                        if (field.referenceType != "ModelResource" && field.referenceType != "Resource")
                        {
                            throw std::invalid_argument(
                                "Native asset reference requires a supported project asset type");
                        }
                    }
                    else
                    {
                        const auto targetCategory = field.referenceType == record->name ?
                                                        record->type.category :
                                                        types.objects().type(field.referenceType).category;
                        if (targetCategory != field.referenceCategory)
                        {
                            throw std::invalid_argument("Native property reference category does not match its type");
                        }
                    }
                }
                for (const auto& other : record->fields)
                {
                    if (&field != &other && (field.name == other.name || field.jsonPath == other.jsonPath ||
                                             field.jsonPath.starts_with(other.jsonPath + "/")))
                    {
                        throw std::invalid_argument("Duplicate or overlapping native property: " + field.name);
                    }
                }
            }
            record->type.name      = record->name.c_str();
            const auto storageName = "vultra.extension." + record->name;
            record->storage        = entt::hashed_string::value(storageName.data(), storageName.size());
            for (const auto* existing : types.objects().types())
            {
                const auto existingName = "vultra.extension." + std::string(existing->name);
                if (entt::hashed_string::value(existingName.data(), existingName.size()) == record->storage)
                {
                    throw std::invalid_argument("Duplicate native type or storage ID: " + record->name);
                }
            }
            records.reserve(records.size() + 1);
            if (record->type.category == "node")
            {
                bindNativeNode(*record);
                record->type.properties = record->properties;
                types.add(record->node);
            }
            else if (record->type.category == "resource")
            {
                bindNativeResource(*record);
                record->type.properties = record->properties;
                types.add(record->resource);
            }
            else
            {
                bindNativeComponent(*record);
                record->type.properties = record->properties;
                types.add(record->component);
            }
            records.push_back(std::move(record));
        }

        void unload()
        {
            for (const auto& record : records)
            {
                if (record->lifetime.catalogs != 1 || record->lifetime.instances != 0)
                {
                    throw std::logic_error("Retire worlds, copied catalogs and instances before unloading " +
                                           record->name);
                }
                for (const auto* type : types.objects().types())
                {
                    if (type->base == &record->type && std::ranges::none_of(records,
                                                                            [&](const auto& own)
                                                                            {
                                                                                return &own->type == type;
                                                                            }))
                    {
                        throw std::logic_error("Another extension derives from " + record->name);
                    }
                }
            }
            for (const auto& record : std::views::reverse(records))
            {
                if (record->type.category == "node")
                {
                    types.remove(record->node);
                }
                else if (record->type.category == "resource")
                {
                    types.remove(record->resource);
                }
                else
                {
                    types.remove(record->component);
                }
            }
            records.clear();
            session.reset();
            plugin.reset();
        }
    };

    NativeExtension::NativeExtension(const std::filesystem::path& path, SceneTypeCatalog& types) :
        m_Impl(std::make_unique<Impl>(types))
    {
        initialize(path, nullptr);
    }

    NativeExtension::NativeExtension(VultraPluginInit entry, SceneTypeCatalog& types) :
        m_Impl(std::make_unique<Impl>(types))
    {
        if (!entry)
        {
            throw std::invalid_argument("Static extension entry is missing");
        }
        initialize({}, entry);
    }

    void NativeExtension::initialize(const std::filesystem::path& path, VultraPluginInit entry)
    {
        m_Impl->registration = {
            VULTRA_ABI_VERSION,
            sizeof(VultraTypeRegistrationApi),
            m_Impl.get(),
            [](void* context, const char* json, uint64_t size, const VultraNativeTypeApi* api, void* factory)
                -> VultraStatus
            {
                if (!context || !json || !api || size == 0)
                {
                    return VULTRA_STATUS_INVALID_ARGUMENT;
                }
                auto& current = *static_cast<Impl*>(context);
                if (!current.registering)
                {
                    return VULTRA_STATUS_INVALID_FRAME;
                }
                try
                {
                    current.add({json, size}, *api, factory);
                    return VULTRA_STATUS_OK;
                }
                catch (const std::exception& error)
                {
                    Logger::app().error("Register native type: {}", error.what());
                    return VULTRA_STATUS_INVALID_ARGUMENT;
                }
            }};
        constexpr std::string_view name = "vultra.types";
        m_Impl->domain                  = {name.data(),
                                           name.size(),
                                           VULTRA_ABI_VERSION,
                                           sizeof(VultraTypeRegistrationApi),
                                           &m_Impl->registration};
        try
        {
            if (entry)
            {
                m_Impl->session = std::make_unique<PluginSession>(entry,
                                                                  nullptr,
                                                                  nullptr,
                                                                  nullptr,
                                                                  nullptr,
                                                                  std::span {&m_Impl->domain, 1});
            }
            else
            {
                m_Impl->plugin = std::make_unique<NativePlugin>(path,
                                                                nullptr,
                                                                nullptr,
                                                                nullptr,
                                                                nullptr,
                                                                std::span {&m_Impl->domain, 1});
            }
            m_Impl->registering = false;
        }
        catch (...)
        {
            m_Impl->registering = false;
            m_Impl->unload();
            throw;
        }
    }

    NativeExtension::~NativeExtension()
    {
        try
        {
            unload();
        }
        catch (const std::exception& error)
        {
            Logger::app().critical("Native extension ownership violation: {}", error.what());
            std::terminate();
        }
    }

    void NativeExtension::unload()
    {
        m_Impl->unload();
    }
} // namespace vultra
