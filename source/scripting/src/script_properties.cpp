#include "script_module.hpp"

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

        struct PropertyFrameScope
        {
            SceneAccess& access;

            explicit PropertyFrameScope(SceneAccess& access) :
                access(access)
            {
                if (access.active)
                {
                    throw std::logic_error("Reentrant script property access");
                }
                ++access.serial;
                access.active = true;
            }

            ~PropertyFrameScope()
            {
                access.active = false;
            }

            PropertyFrameScope(const PropertyFrameScope&)            = delete;
            PropertyFrameScope& operator=(const PropertyFrameScope&) = delete;

            VultraSceneFrame frame() const
            {
                return {&access, access.serial};
            }
        };

        template<class Read>
        std::string readCallback(const char* operation, Read read)
        {
            uint64_t size   = 0;
            auto     status = read(nullptr, 0, &size);
            if (status != VULTRA_STATUS_OK)
            {
                throw std::runtime_error(std::string(operation) + ": API status " + std::to_string(int(status)));
            }
            std::string result(size, '\0');
            status = read(result.data(), result.size(), &size);
            if (status != VULTRA_STATUS_OK || size > result.size())
            {
                throw std::runtime_error(std::string(operation) + ": API status " + std::to_string(int(status)));
            }
            result.resize(size);
            return result;
        }
    } // namespace

    const PropertySchema& ScriptClassInfo::property(std::string_view name) const
    {
        const auto found = std::ranges::find(properties, name, &PropertySchema::name);
        if (found == properties.end())
        {
            throw std::invalid_argument(this->name + ": unknown script export " + std::string(name));
        }
        return *found;
    }

    void ScriptInstance::bindProperties(const VultraScriptPropertyApi* api,
                                        void*                          state,
                                        SceneTree&                     scene,
                                        const ProjectManifest*         project)
    {
        if (!api || !state || api->version != VULTRA_ABI_VERSION ||
            api->struct_size != sizeof(VultraScriptPropertyApi) || !api->describe || !api->read || !api->write)
        {
            throw std::invalid_argument("Script requires a matching property API");
        }
        try
        {
            const auto text     = readCallback("Describe script class",
                                               [&](char* data, uint64_t capacity, uint64_t* size)
                                               {
                                               return api->describe(state, data, capacity, size);
                                               });
            const auto document = Json::parse(text);
            if (!document.is_object() || !document.at("version").is_number_integer() || document.at("version") != 1 ||
                document.at("category") != "script" || !document.at("properties").is_array())
            {
                throw std::invalid_argument("Script class requires a version-1 property description");
            }
            for (const auto& [key, value] : document.items())
            {
                if (key != "version" && key != "category" && key != "name" && key != "base" && key != "properties" &&
                    key != "classes" && key != "dependencies" && key != "callbacks")
                {
                    throw std::invalid_argument("Unknown script class field: " + key);
                }
            }
            ScriptClassInfo candidate {document.at("name").get<std::string>(),
                                       document.at("base").get<std::string>(),
                                       {},
                                       {},
                                       {}};
            if (!document.at("dependencies").is_array())
            {
                throw std::invalid_argument("Script dependencies require an array of source paths");
            }
            candidate.dependencies = document.at("dependencies").get<std::vector<std::string>>();
            if (!document.at("callbacks").is_array())
            {
                throw std::invalid_argument("Script callbacks require an array");
            }
            for (const auto& callback : document.at("callbacks"))
            {
                const auto name = callback.get<std::string>();
                if (name == "input" && !candidate.input)
                {
                    candidate.input = true;
                }
                else if (name == "unhandled_input" && !candidate.unhandledInput)
                {
                    candidate.unhandledInput = true;
                }
                else
                {
                    throw std::invalid_argument("Unknown or duplicate script callback: " + name);
                }
            }
            if (candidate.name.empty() || candidate.baseType.empty())
            {
                throw std::invalid_argument("Script class requires a name and a native base type");
            }
            if (!document.at("classes").is_array() || document.at("classes").empty())
            {
                throw std::invalid_argument("Script class requires its declared ancestry");
            }
            for (const auto& type : document.at("classes"))
            {
                if (!type.is_object() || type.size() != 2 || !type.contains("module") || !type.contains("name"))
                {
                    throw std::invalid_argument("Script ancestry requires only module/name identities");
                }
                candidate.classes.push_back({type.at("module").get<std::string>(), type.at("name").get<std::string>()});
            }
            if (!candidate.classes.front().module.empty() || candidate.classes.front().name != candidate.name)
            {
                throw std::invalid_argument("Script class ancestry must begin with its own declaration");
            }
            for (const auto& value : document.at("properties"))
            {
                auto property = parsePropertySchema(Json {{"version", 1}, {"property", value}}.dump());
                if (property.kind == PropertyKind::eReference ||
                    (property.kind == PropertyKind::eArray && property.elementKind == PropertyKind::eReference))
                {
                    const auto& target = scene.types().objects().type(property.referenceType);
                    if (target.category != property.referenceCategory)
                    {
                        throw std::invalid_argument("Script reference has the wrong target category: " + property.name);
                    }
                }
                for (const auto& existing : candidate.properties)
                {
                    if (existing.name == property.name || existing.jsonPath == property.jsonPath ||
                        existing.jsonPath.starts_with(property.jsonPath + "/") ||
                        property.jsonPath.starts_with(existing.jsonPath + "/"))
                    {
                        throw std::invalid_argument("Duplicate/overlapping script export: " + property.name);
                    }
                }
                candidate.properties.push_back(std::move(property));
            }
            m_Class                  = std::move(candidate);
            m_PropertyApi            = api;
            m_PropertyState          = state;
            m_PropertyAccess.scene   = &scene;
            m_PropertyAccess.project = project;
            m_PropertyBinding        = {m_Class.properties,
                                        this,
                                        [](void* instance, std::string_view name)
                                        {
                                     return static_cast<ScriptInstance*>(instance)->readExport(name);
                                        },
                                        [](void* instance, std::string_view name, const PropertyValue& value)
                                        {
                                     static_cast<ScriptInstance*>(instance)->writeExport(name, value);
                                        },
                                        m_Class.classes};
        }
        catch (const Json::exception& error)
        {
            throw std::invalid_argument(std::string("Invalid script class metadata: ") + error.what());
        }
    }

    const ScriptClassInfo& ScriptInstance::scriptClass() const
    {
        if (!m_PropertyApi)
        {
            throw std::logic_error("Module has no script instance property API");
        }
        return m_Class;
    }

    void ScriptInstance::bindClassOrigin(const std::filesystem::path& source)
    {
        const auto qualify = [&](PropertyClassId& type)
        {
            if (type.name.empty() || type.name.find('\0') != std::string::npos ||
                type.module.find('\0') != std::string::npos)
            {
                throw std::invalid_argument("Script class requires a valid module/name identity");
            }
            type.module = type.module.empty() ?
                              source.generic_string() :
                              std::filesystem::canonical(source.parent_path() / type.module).generic_string();
        };
        for (auto& type : m_Class.classes)
        {
            qualify(type);
        }
        for (auto& property : m_Class.properties)
        {
            if (property.referenceClass)
            {
                qualify(*property.referenceClass);
            }
        }
        for (auto& dependency : m_Class.dependencies)
        {
            if (dependency.empty() || dependency.find('\0') != std::string::npos)
            {
                throw std::invalid_argument("Script dependency requires a source path");
            }
            dependency = std::filesystem::canonical(source.parent_path() / dependency).generic_string();
        }
        std::ranges::sort(m_Class.dependencies);
        const auto duplicates = std::ranges::unique(m_Class.dependencies);
        m_Class.dependencies.erase(duplicates.begin(), duplicates.end());
    }

    const NodePropertyBinding& ScriptInstance::propertyBinding() const
    {
        return m_PropertyBinding;
    }

    void ScriptInstance::prepareProperties()
    {
        if (m_PropertyApi->prepare)
        {
            PropertyFrameScope scope(m_PropertyAccess);
            const auto         status = m_PropertyApi->prepare(m_PropertyState, scope.frame());
            if (status != VULTRA_STATUS_OK)
            {
                throw std::runtime_error("Prepare script references: API status " + std::to_string(int(status)));
            }
        }
    }

    PropertyValue ScriptInstance::readExport(std::string_view name) const
    {
        const auto& property = scriptClass().property(name);
        const auto  text =
            readCallback("Read script export",
                         [&](char* data, uint64_t capacity, uint64_t* size)
                         {
                             PropertyFrameScope scope(m_PropertyAccess);
                             return m_PropertyApi
                                 ->read(m_PropertyState, scope.frame(), name.data(), name.size(), data, capacity, size);
                         });
        auto value = deserializeProperty(property, text);
        m_PropertyAccess.scene->validatePropertyValue(property, value, m_PropertyAccess.project);
        return value;
    }

    void ScriptInstance::writeExport(std::string_view name, const PropertyValue& value)
    {
        const auto&        property = scriptClass().property(name);
        const auto         text     = serializeProperty(property, value);
        PropertyFrameScope scope(m_PropertyAccess);
        const auto         status =
            m_PropertyApi->write(m_PropertyState, scope.frame(), name.data(), name.size(), text.data(), text.size());
        if (status != VULTRA_STATUS_OK)
        {
            throw std::runtime_error("Write script export " + property.name + ": API status " +
                                     std::to_string(int(status)));
        }
    }
} // namespace vultra
