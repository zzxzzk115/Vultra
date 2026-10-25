#include "script_module.hpp"
#ifdef VULTRA_SCRIPT_LUA
#include "lua_context.hpp"
#endif
#ifdef VULTRA_SCRIPT_NATIVE
#include "native_script_module.hpp"
#endif

#include <vultra/assets/asset_source.hpp>
#include <vultra/core/base/logger.hpp>
#include <vultra/scene/scene_components.hpp>
#include <vultra/scene/scene_tree.hpp>
#include <vultra/scripting/script_host.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vultra
{
    namespace
    {
        struct FileStamp
        {
            std::filesystem::file_time_type modified;
            uintmax_t                       size;
            friend bool                     operator==(const FileStamp&, const FileStamp&) = default;
        };

        std::optional<FileStamp> fileStamp(const std::filesystem::path& path)
        {
            std::error_code error;
            const auto      modified = std::filesystem::last_write_time(path, error);
            if (error)
            {
                return std::nullopt;
            }
            const auto size = std::filesystem::file_size(path, error);
            if (error)
            {
                return std::nullopt;
            }
            return FileStamp {modified, size};
        }

        ObjectId scriptNode(const ScriptAttachment& module, SceneTree& scene)
        {
            const auto* node = module.node ? scene.find(NodeId {*module.node}) : &scene.root();
            if (!node)
            {
                throw std::invalid_argument("Script target node is missing: " + module.node->toString());
            }
            return node->id();
        }

    } // namespace

    struct ScriptHost::Impl
    {
        struct ScriptEntry
        {
            ScriptLanguage language;
            std::string    typeName;
        };

        struct SourceStamp
        {
            std::filesystem::path path;
            FileStamp             stamp;
        };

        struct Slot
        {
            std::optional<ScriptEntry> module;
            std::optional<size_t>      provider;
            std::filesystem::path      path;
            FileStamp                  loaded;
            // Failed attempts remember every input, including a missing file, without polling allocations.
            std::optional<std::vector<std::optional<FileStamp>>> attempted;
            std::unique_ptr<ScriptInstance>                      instance;
            ObjectId                                             target {};
            std::vector<ScriptExport>                            exports;
            std::vector<SourceStamp>                             dependencies;
            bool                                                 authored = false;
        };

        Impl(SceneTree& scene, bool hotReload, const ProjectManifest* project) :
            scene(scene),
            project(project),
            hotReload(hotReload)
        {
        }

#ifdef VULTRA_SCRIPT_LUA
        LuaContext& luaContext()
        {
            if (!lua)
            {
                lua = std::make_unique<LuaContext>(scene, project);
            }
            return *lua;
        }
#endif

        std::unique_ptr<ScriptInstance> loadScript(const ScriptEntry&           module,
                                                   const std::filesystem::path& path,
                                                   ObjectId                     node,
                                                   const ScriptInstance*        previous  = nullptr,
                                                   const ScriptInstance*        peer      = nullptr,
                                                   const ScriptInstance*        batchPeer = nullptr)
        {
            switch (module.language)
            {
                case ScriptLanguage::eNative:
#ifdef VULTRA_SCRIPT_NATIVE
                    return loadNativeScript(path, scene, hotReload, node, module.typeName, project);
#else
                    throw std::invalid_argument("Native scripts require the script_native feature");
#endif
                case ScriptLanguage::eLua:
#ifdef VULTRA_SCRIPT_LUA
                    return loadLuaScript(path, luaContext(), node);
#else
                    throw std::invalid_argument("Lua scripts require the script_lua feature");
#endif
                case ScriptLanguage::eCSharp: {
#ifdef VULTRA_SCRIPT_CSHARP
                    if (module.typeName.empty())
                    {
                        throw std::invalid_argument("C# script requires a class name and scene node ID");
                    }
                    return loadDotNetScript(path, scene, node, module.typeName, previous, project, peer, batchPeer);
#else
                    throw std::invalid_argument("C# scripts require the script_csharp feature");
#endif
                }
            }
            throw std::invalid_argument("Unknown script language");
        }

        Slot& scriptSlot(ObjectId id)
        {
            if (stopped)
            {
                throw std::logic_error("Access exports of a stopped script host");
            }
            if (!scene.find(id))
            {
                throw std::invalid_argument("Script target does not belong to this world");
            }
            const auto found = std::ranges::find_if(scripts,
                                                    [&](const Slot& slot)
                                                    {
                                                        return slot.target == id && slot.instance && slot.module;
                                                    });
            if (found == scripts.end() ||
                scene.registry().get<SceneProcess>(scene.entity(id)).behavior != found->instance.get())
            {
                throw std::invalid_argument("Node has no script instance");
            }
            return *found;
        }

        void applyExports(const ScriptEntry&                          module,
                          ScriptInstance&                             instance,
                          ObjectId                                    target,
                          std::span<const ScriptExport>               values,
                          std::span<const SceneTree::BehaviorBinding> proposed = {})
        {
            const auto& info   = instance.scriptClass();
            const auto& base   = scene.types().objects().type(info.baseType);
            const auto& native = scene.objectType(target);
            if (base.category != "node" ||
                !scene.types().objects().type(scene.find(target)->typeName()).isA(info.baseType) ||
                (!module.typeName.empty() && module.typeName != info.name))
            {
                throw std::invalid_argument("Script class/base does not match its target: " + info.name);
            }
            for (const auto& property : info.properties)
            {
                if (std::ranges::find(native.properties, property.name, &PropertyInfo::name) != native.properties.end())
                {
                    throw std::invalid_argument("Script export collides with a native field: " + property.name);
                }
            }
            for (const auto& value : values)
            {
                const auto& property = info.property(value.name);
                if (!hasPropertyFlag(property.flags, PropertyFlags::eSerialize) ||
                    std::ranges::count(values, value.name, &ScriptExport::name) != 1)
                {
                    throw std::invalid_argument("Initial script exports require unique serializable fields: " +
                                                value.name);
                }
                scene.validatePropertyValue(property, value.value, project, proposed);
            }
            std::vector<ScriptExport> initial;
            initial.reserve(info.properties.size());
            for (const auto& property : info.properties)
            {
                if (!hasPropertyFlag(property.flags, PropertyFlags::eSerialize))
                {
                    continue;
                }
                const auto  found = std::ranges::find(values, property.name, &ScriptExport::name);
                const auto& value = found == values.end() ? property.defaultValue : found->value;
                scene.validatePropertyValue(property, value, project, proposed);
                initial.push_back({property.name, value});
            }
            for (const auto& value : initial)
            {
                instance.writeExport(value.name, value.value);
            }
        }

        Slot stageAttachment(const ScriptAttachment& request, std::span<const Slot> pending, bool authored)
        {
            const ScriptEntry module {request.language, request.typeName};
            const auto        target = scriptNode(request, scene);
            if (!authored && scene.find(target)->script())
            {
                throw std::invalid_argument("Use addScene for a scene-owned script attachment");
            }
            const auto duplicate = [target](const Slot& slot)
            {
                return slot.module && slot.instance && slot.target == target;
            };
            if (std::ranges::any_of(scripts, duplicate) || std::ranges::any_of(pending, duplicate))
            {
                throw std::invalid_argument("A scene node can have only one script attachment");
            }
            const auto stamp = fileStamp(request.path);
            if (!stamp)
            {
                throw std::runtime_error("Script file is missing: " + request.path.string());
            }
            const auto                      source = std::filesystem::canonical(request.path);
            std::optional<size_t>           provider;
            std::unique_ptr<ScriptInstance> instance;
#ifdef VULTRA_SCRIPT_NATIVE
            if (module.language == ScriptLanguage::eNative && !module.typeName.empty())
            {
                for (size_t index = 0; index < scripts.size(); ++index)
                {
                    const auto& slot = scripts[index];
                    if (slot.module || slot.path != source)
                    {
                        continue;
                    }
                    const auto& native = static_cast<const NativeScriptProvider&>(*slot.instance);
                    if (!native.hasClass(module.typeName))
                    {
                        throw std::invalid_argument("Extension does not register script class: " + module.typeName);
                    }
                    provider = index;
                    instance = loadProvidedNativeScript(native.entry(), scene, target, module.typeName, project);
                    break;
                }
            }
#endif
            if (!instance)
            {
                const ScriptInstance* peer      = nullptr;
                const ScriptInstance* batchPeer = nullptr;
                if (module.language == ScriptLanguage::eCSharp)
                {
                    const auto findPeer = [](std::span<const Slot> slots) -> const ScriptInstance*
                    {
                        for (const auto& slot : slots)
                        {
                            if (slot.module && slot.module->language == ScriptLanguage::eCSharp && slot.instance)
                            {
                                return slot.instance.get();
                            }
                        }
                        return nullptr;
                    };
                    peer      = findPeer(scripts);
                    batchPeer = findPeer(pending);
                    if (!peer)
                    {
                        peer = findPeer(pending);
                    }
                }
                instance = loadScript(module, source, target, nullptr, peer, batchPeer);
            }
            instance->bindClassOrigin(source);
            auto dependencies = dependencyStamps(*instance, source);
            auto exports      = authored ? decodeExports(*instance, *scene.find(target)->script()) :
                                           std::vector<ScriptExport>(request.exports.begin(), request.exports.end());
            return {module,
                    provider,
                    source,
                    *stamp,
                    std::nullopt,
                    std::move(instance),
                    target,
                    std::move(exports),
                    std::move(dependencies),
                    authored};
        }

        static std::vector<ScriptExport> decodeExports(const ScriptInstance& instance, const SceneScript& script)
        {
            const auto                values = nlohmann::json::parse(script.exports);
            std::vector<ScriptExport> exports;
            exports.reserve(values.size());
            for (const auto& [name, value] : values.items())
            {
                try
                {
                    const auto& property = instance.scriptClass().property(name);
                    exports.push_back({name, deserializeProperty(property, value.dump())});
                }
                catch (const std::exception& error)
                {
                    throw std::invalid_argument("Initial export " + script.className + "." + name + ": " +
                                                error.what());
                }
            }
            return exports;
        }

        static std::vector<SourceStamp> dependencyStamps(const ScriptInstance&        instance,
                                                         const std::filesystem::path& source)
        {
            std::vector<SourceStamp> result;
            for (const auto& path : instance.scriptClass().dependencies)
            {
                if (path == source.generic_string())
                {
                    continue;
                }
                const auto stamp = fileStamp(path);
                if (!stamp)
                {
                    throw std::runtime_error("Script dependency is missing: " + path);
                }
                result.push_back({path, *stamp});
            }
            return result;
        }

        static void verifySources(const Slot& slot)
        {
            if (fileStamp(slot.path) != slot.loaded)
            {
                throw std::runtime_error("Script source changed during staging: " + slot.path.string());
            }
            for (const auto& dependency : slot.dependencies)
            {
                if (fileStamp(dependency.path) != dependency.stamp)
                {
                    throw std::runtime_error("Script dependency changed during staging: " + dependency.path.string());
                }
            }
        }

        static bool sourceChanged(const Slot& slot, bool retry)
        {
            if (slot.attempted && retry)
            {
                return true;
            }
            const auto root = slot.attempted ? (*slot.attempted)[0] : std::optional(slot.loaded);
            if (fileStamp(slot.path) != root)
            {
                return true;
            }
            for (size_t index = 0; index < slot.dependencies.size(); ++index)
            {
                const auto& dependency = slot.dependencies[index];
                const auto  expected = slot.attempted ? (*slot.attempted)[index + 1] : std::optional(dependency.stamp);
                if (fileStamp(dependency.path) != expected)
                {
                    return true;
                }
            }
            return false;
        }

        static void recordAttempt(Slot& slot)
        {
            std::vector<std::optional<FileStamp>> stamps;
            stamps.reserve(slot.dependencies.size() + 1);
            stamps.push_back(fileStamp(slot.path));
            for (const auto& dependency : slot.dependencies)
            {
                stamps.push_back(fileStamp(dependency.path));
            }
            slot.attempted = std::move(stamps);
        }

        void retireMissingNodes()
        {
            for (auto& slot : scripts)
            {
                if (!slot.instance || !slot.module)
                {
                    continue;
                }
                if (scene.find(slot.target) &&
                    scene.registry().get<SceneProcess>(scene.entity(slot.target)).behavior == slot.instance.get())
                {
                    continue;
                }
                slot.instance->stop();
                slot.instance.reset();
                Logger::app().info("Detached script from object: {}", slot.target.value);
            }
        }

        std::vector<size_t> connectedGroup(size_t root) const
        {
            std::vector<std::filesystem::path> sources {scripts[root].path};
            const auto                         addSource = [&](const std::filesystem::path& source)
            {
                if (std::ranges::find(sources, source) == sources.end())
                {
                    sources.push_back(source);
                }
            };
            for (size_t cursor = 0; cursor < sources.size(); ++cursor)
            {
                const auto source = sources[cursor].generic_string();
                for (const auto& slot : scripts)
                {
                    if (!slot.instance || !slot.module)
                    {
                        continue;
                    }
                    const auto& type  = slot.instance->scriptClass();
                    const auto  visit = [&](std::string_view dependency)
                    {
                        if (slot.path.generic_string() == source)
                        {
                            addSource(std::filesystem::path(dependency));
                        }
                        else if (dependency == source)
                        {
                            addSource(slot.path);
                        }
                    };
                    for (const auto& property : type.properties)
                    {
                        if (property.referenceClass)
                        {
                            visit(property.referenceClass->module);
                        }
                    }
                    for (const auto& declared : type.classes)
                    {
                        visit(declared.module);
                    }
                    for (const auto& dependency : type.dependencies)
                    {
                        visit(dependency);
                    }
                }
            }
            std::vector<size_t> indices;
            for (size_t index = 0; index < scripts.size(); ++index)
            {
                if (scripts[index].instance && std::ranges::find(sources, scripts[index].path) != sources.end())
                {
                    indices.push_back(index);
                }
            }
            return indices;
        }

        std::vector<SourceStamp> replaceGroup(std::vector<size_t>& indices)
        {
            struct Replacement
            {
                size_t                          index;
                std::unique_ptr<ScriptInstance> instance;
                std::vector<SourceStamp>        dependencies;
            };

            // Staged instances must be destroyed before the library that supplies their callbacks.
            std::vector<SourceStamp> sources;
            for (const auto index : indices)
            {
                recordAttempt(scripts[index]);
            }
            for (const auto index : indices)
            {
                const auto& source = scripts[index].path;
                if (std::ranges::find(sources, source, &SourceStamp::path) == sources.end())
                {
                    const auto stamp = fileStamp(source);
                    if (!stamp)
                    {
                        throw std::runtime_error("Reload source is missing: " + source.string());
                    }
                    sources.push_back({source, *stamp});
                }
            }
#ifdef VULTRA_SCRIPT_LUA
            std::optional<LuaContext::ReloadScope> luaReload;
            if (lua && std::ranges::any_of(indices,
                                           [&](size_t index)
                                           {
                                               return scripts[index].module &&
                                                      scripts[index].module->language == ScriptLanguage::eLua;
                                           }))
            {
                std::vector<std::filesystem::path> changed;
                for (const auto index : indices)
                {
                    const auto& slot = scripts[index];
                    if (fileStamp(slot.path) != slot.loaded)
                    {
                        changed.push_back(slot.path);
                    }
                    for (const auto& dependency : slot.dependencies)
                    {
                        if (fileStamp(dependency.path) != dependency.stamp)
                        {
                            changed.push_back(dependency.path);
                        }
                    }
                }
                luaReload.emplace(*lua, changed);
            }
#endif
            std::vector<Replacement> providers;
            providers.reserve(indices.size());
            std::vector<Replacement> replacements;
            replacements.reserve(indices.size());
            const ScriptInstance* managedPeer = nullptr;
            // Providers precede their users in the slot list.
            for (size_t cursor = 0; cursor < indices.size(); ++cursor)
            {
                const auto                      index = indices[cursor];
                const auto&                     slot  = scripts[index];
                std::unique_ptr<ScriptInstance> instance;
                if (slot.module && !slot.provider)
                {
                    const auto isManaged = slot.module->language == ScriptLanguage::eCSharp;
                    instance             = loadScript(*slot.module,
                                                      slot.path,
                                                      slot.target,
                                                      slot.instance.get(),
                                                      nullptr,
                                                      isManaged ? managedPeer : nullptr);
                    if (isManaged && !managedPeer)
                    {
                        managedPeer = instance.get();
                    }
                }
                else
                {
#ifdef VULTRA_SCRIPT_NATIVE
                    if (slot.provider)
                    {
                        const auto provider = std::ranges::find(providers, *slot.provider, &Replacement::index);
                        if (provider == providers.end())
                        {
                            throw std::logic_error("Native script provider was not staged before its instances");
                        }
                        const auto& native = static_cast<const NativeScriptProvider&>(*provider->instance);
                        if (!native.hasClass(slot.module->typeName))
                        {
                            throw std::invalid_argument("Extension no longer registers script class: " +
                                                        slot.module->typeName);
                        }
                        instance = loadProvidedNativeScript(native.entry(),
                                                            scene,
                                                            slot.target,
                                                            slot.module->typeName,
                                                            project);
                    }
                    else
                    {
                        auto candidate = std::make_unique<NativeScriptProvider>(slot.path, scene, hotReload, project);
                        for (size_t otherIndex = 0; otherIndex < scripts.size(); ++otherIndex)
                        {
                            const auto& other = scripts[otherIndex];
                            if (std::ranges::find(indices, otherIndex) != indices.end() || other.module ||
                                !other.instance)
                            {
                                continue;
                            }
                            const auto& existing = static_cast<const NativeScriptProvider&>(*other.instance);
                            for (const auto& name : candidate->classes())
                            {
                                if (existing.hasClass(name))
                                {
                                    throw std::invalid_argument("Duplicate extension script class: " + name);
                                }
                            }
                        }
                        for (const auto& other : providers)
                        {
                            const auto& existing = static_cast<const NativeScriptProvider&>(*other.instance);
                            for (const auto& name : candidate->classes())
                            {
                                if (existing.hasClass(name))
                                {
                                    throw std::invalid_argument("Duplicate extension script class: " + name);
                                }
                            }
                        }
                        providers.push_back({index, std::move(candidate)});
                        continue;
                    }
#else
                    throw std::logic_error("Native extensions require the script_native feature");
#endif
                }
                instance->bindClassOrigin(slot.path);
                auto dependencies = dependencyStamps(*instance, slot.path);
                for (const auto& dependency : dependencies)
                {
                    if (std::ranges::find(sources, dependency.path, &SourceStamp::path) == sources.end())
                    {
                        sources.push_back(dependency);
                    }
                    // New dependencies bring their existing users into the same code/cache generation.
                    for (size_t related = 0; related < scripts.size(); ++related)
                    {
                        const auto& existing = scripts[related];
                        const auto  depends  = existing.module && existing.instance &&
                                               std::ranges::find(existing.instance->scriptClass().dependencies,
                                                                 dependency.path.generic_string()) !=
                                                   existing.instance->scriptClass().dependencies.end();
                        if (existing.instance && (existing.path == dependency.path || depends))
                        {
                            for (const auto member : connectedGroup(related))
                            {
                                if (std::ranges::find(indices, member) == indices.end())
                                {
                                    recordAttempt(scripts[member]);
                                    indices.push_back(member);
                                    if (std::ranges::find(sources, scripts[member].path, &SourceStamp::path) ==
                                        sources.end())
                                    {
                                        const auto stamp = fileStamp(scripts[member].path);
                                        if (!stamp)
                                        {
                                            throw std::runtime_error("Reload source is missing: " +
                                                                     scripts[member].path.string());
                                        }
                                        sources.push_back({scripts[member].path, *stamp});
                                    }
                                }
                            }
                        }
                    }
                }
                replacements.push_back({index, std::move(instance), std::move(dependencies)});
            }
            std::vector<SceneTree::BehaviorBinding> bindings;
            bindings.reserve(replacements.size());
            for (const auto& candidate : replacements)
            {
                const auto& slot = scripts[candidate.index];
                bindings.push_back({*scene.find(slot.target),
                                    slot.instance.get(),
                                    *candidate.instance,
                                    &candidate.instance->propertyBinding()});
            }
            for (const auto& candidate : replacements)
            {
                candidate.instance->prepareProperties();
            }
            for (const auto& candidate : replacements)
            {
                const auto& slot = scripts[candidate.index];
                if (slot.authored)
                {
                    const auto exports = decodeExports(*candidate.instance, *scene.find(slot.target)->script());
                    applyExports(*slot.module, *candidate.instance, slot.target, exports, bindings);
                }
                else
                {
                    applyExports(*slot.module, *candidate.instance, slot.target, slot.exports, bindings);
                }
            }
            for (const auto& source : sources)
            {
                if (fileStamp(source.path) != source.stamp)
                {
                    throw std::runtime_error("Module file changed while staging its instances: " +
                                             source.path.string());
                }
            }
            scene.bindBehaviors(bindings);
#ifdef VULTRA_SCRIPT_LUA
            if (luaReload)
            {
                luaReload->commit();
            }
#endif
            // All fallible staging is complete. Retire instances before unloading any provider code.
            for (auto it = replacements.rbegin(); it != replacements.rend(); ++it)
            {
                auto& previous = scripts[it->index].instance;
                previous->stop();
                previous.reset();
            }
            for (auto& provider : providers)
            {
                auto& previous = scripts[provider.index].instance;
                previous->stop();
                previous = std::move(provider.instance);
            }
            for (auto& candidate : replacements)
            {
                scripts[candidate.index].instance     = std::move(candidate.instance);
                scripts[candidate.index].dependencies = std::move(candidate.dependencies);
            }
            return sources;
        }

        void add(std::span<const ScriptAttachment> attachments, bool authored)
        {
            if (stopped)
            {
                throw std::logic_error("Add script to stopped host");
            }
            if (scene.running())
            {
                throw std::logic_error("Attach scripts before starting the scene");
            }
            retireMissingNodes();
#ifdef VULTRA_SCRIPT_LUA
            std::optional<LuaContext::ReloadScope> luaReload;
            if (std::ranges::any_of(attachments,
                                    [](const ScriptAttachment& attachment)
                                    {
                                        return attachment.language == ScriptLanguage::eLua;
                                    }))
            {
                luaReload.emplace(luaContext(), std::span<const std::filesystem::path> {}, false);
            }
#endif
            std::vector<Slot> pending;
            pending.reserve(attachments.size());
            for (const auto& request : attachments)
            {
                pending.push_back(stageAttachment(request, pending, authored));
            }
            std::vector<SceneTree::BehaviorBinding> bindings;
            bindings.reserve(pending.size());
            for (const auto& slot : pending)
            {
                bindings.push_back(
                    {*scene.find(slot.target), nullptr, *slot.instance, &slot.instance->propertyBinding()});
            }
            for (const auto& slot : pending)
            {
                slot.instance->prepareProperties();
            }
            for (const auto& slot : pending)
            {
                applyExports(*slot.module, *slot.instance, slot.target, slot.exports, bindings);
            }
            for (auto& slot : pending)
            {
                verifySources(slot);
                if (slot.authored)
                {
                    slot.exports.clear();
                }
            }
            scripts.reserve(scripts.size() + pending.size());
            scene.bindBehaviors(bindings);
#ifdef VULTRA_SCRIPT_LUA
            if (luaReload)
            {
                luaReload->commit();
            }
#endif
            for (auto& slot : pending)
            {
                scripts.push_back(std::move(slot));
            }
        }

        SceneTree&             scene;
        const ProjectManifest* project = nullptr;
#ifdef VULTRA_SCRIPT_LUA
        std::unique_ptr<LuaContext> lua;
#endif
        std::vector<Slot>     scripts;
        std::string           reloadError;
        std::filesystem::path reloadErrorPath;
        bool                  hotReload = false;
        bool                  stopped   = false;
    };

    ScriptHost::ScriptHost(SceneTree& scene, bool hotReload, const ProjectManifest* project) :
        m_Impl(std::make_unique<Impl>(scene, hotReload, project))
    {
    }

    ScriptHost::~ScriptHost()
    {
        stop();
    }

    void ScriptHost::addNativeModule(const std::filesystem::path& path)
    {
        if (m_Impl->stopped)
        {
            throw std::logic_error("Add extension to stopped host");
        }
#ifdef VULTRA_SCRIPT_NATIVE
        const auto stamp = fileStamp(path);
        if (!stamp)
        {
            throw std::runtime_error("Extension file is missing: " + path.string());
        }
        const auto source = std::filesystem::canonical(path);
        auto       instance =
            std::make_unique<NativeScriptProvider>(source, m_Impl->scene, m_Impl->hotReload, m_Impl->project);
        for (const auto& slot : m_Impl->scripts)
        {
            if (!slot.instance)
            {
                continue;
            }
            if (slot.module)
            {
                continue;
            }
            if (slot.path == source)
            {
                throw std::invalid_argument("Duplicate project extension path: " + path.string());
            }
            const auto& existing = *static_cast<const NativeScriptProvider*>(slot.instance.get());
            for (const auto& name : instance->classes())
            {
                if (existing.hasClass(name))
                {
                    throw std::invalid_argument("Duplicate extension script class: " + name);
                }
            }
        }
        m_Impl->scripts.push_back({std::nullopt, std::nullopt, source, *stamp, std::nullopt, std::move(instance)});
#else
        throw std::invalid_argument("Native script modules require the script_native feature: " + path.string());
#endif
    }

    void ScriptHost::add(const ScriptAttachment& attachment)
    {
        add(std::span(&attachment, 1));
    }

    void ScriptHost::add(std::span<const ScriptAttachment> attachments)
    {
        m_Impl->add(attachments, false);
    }

    void ScriptHost::addScene(const AssetSource& source)
    {
        if (m_Impl->stopped)
        {
            throw std::logic_error("Add scene scripts to stopped host");
        }
        if (!m_Impl->project)
        {
            throw std::logic_error("Scene script attachments require their owning project");
        }
        if (m_Impl->scene.running())
        {
            throw std::logic_error("Attach scripts before starting the scene");
        }
        m_Impl->scene.validateAssets(*m_Impl->project);
        for (const auto& module : m_Impl->project->scripts)
        {
            if (module.language != ScriptLanguage::eNative)
            {
                continue;
            }
            const auto path = m_Impl->project->materializeModule(source, m_Impl->project->asset(module.asset).path);
            const auto canonical = std::filesystem::canonical(path);
            const auto found     = std::ranges::find_if(m_Impl->scripts,
                                                        [&](const Impl::Slot& slot)
                                                        {
                                                        return !slot.module && slot.path == canonical;
                                                        });
            if (found == m_Impl->scripts.end())
            {
                addNativeModule(path);
            }
            else if (fileStamp(canonical) != found->loaded)
            {
                throw std::runtime_error("Reload native modules before attaching a changed source: " + path.string());
            }
        }
        std::vector<ScriptAttachment> attachments;
        for (const auto entity : m_Impl->scene.orderedEntities())
        {
            const auto& node = m_Impl->scene.registry().get<SceneNode>(entity);
            if (!node.node || !node.node->script())
            {
                continue;
            }
            const auto& script = *node.node->script();
            const auto& module = m_Impl->project->scriptModule(script.module);
            const auto& path   = m_Impl->project->asset(module.asset).path;
            attachments.push_back({module.language,
                                   m_Impl->project->materializeModule(source, path),
                                   script.className,
                                   node.persistentId.value});
#ifdef VULTRA_SCRIPT_LUA
            if (module.language == ScriptLanguage::eLua)
            {
                auto root = attachments.back().path;
                for (auto part = path.begin(); part != path.end(); ++part)
                {
                    root = root.parent_path();
                }
                m_Impl->luaContext().setModuleRoot(root);
            }
#endif
        }
        m_Impl->add(attachments, true);
    }

    void ScriptHost::captureExports(ObjectId id)
    {
        if (m_Impl->stopped)
        {
            throw std::logic_error("Capture exports of a stopped host");
        }
        if (m_Impl->scene.running())
        {
            throw std::logic_error("Capture authored exports in a stopped world");
        }
        const auto& slot = m_Impl->scriptSlot(id);
        auto*       node = m_Impl->scene.find(id);
        if (!slot.authored || !node->script())
        {
            throw std::invalid_argument("Capture exports requires a scene-owned script attachment");
        }
        auto script = *node->script();
        auto values = nlohmann::json::object();
        for (const auto& property : slot.instance->scriptClass().properties)
        {
            if (hasPropertyFlag(property.flags, PropertyFlags::eSerialize))
            {
                const auto value = readExport(id, property.name);
                if (value != property.defaultValue)
                {
                    values[property.name] = nlohmann::json::parse(serializeProperty(property, value));
                }
            }
        }
        script.exports = values.dump();
        node->setScript(std::move(script));
    }

    size_t ScriptHost::reloadChanged(bool retryFailed)
    {
        if (m_Impl->stopped)
        {
            throw std::logic_error("Reload stopped script host");
        }
        if (!m_Impl->hotReload)
        {
            throw std::logic_error("Reload requires a hot-reload script host");
        }
        m_Impl->retireMissingNodes();
        size_t              count = 0;
        std::vector<size_t> handled;
        for (size_t index = 0; index < m_Impl->scripts.size(); ++index)
        {
            auto& slot = m_Impl->scripts[index];
            if (slot.provider || !slot.instance || std::ranges::find(handled, index) != handled.end())
            {
                continue;
            }
            // Restoring the last good file can also restore its timestamp after a failed reload.
            if (!Impl::sourceChanged(slot, retryFailed))
            {
                continue;
            }
            auto                           group = m_Impl->connectedGroup(index);
            std::vector<Impl::SourceStamp> sources;
            bool                           committed = false;
            try
            {
                sources   = m_Impl->replaceGroup(group);
                committed = true;
            }
            catch (const std::exception& error)
            {
                m_Impl->reloadErrorPath = slot.path;
                m_Impl->reloadError     = "Reload " + slot.path.string() + ": " + error.what();
                Logger::app().error("{}", m_Impl->reloadError);
            }
            handled.insert(handled.end(), group.begin(), group.end());
            if (!committed)
            {
                continue;
            }
            for (const auto member : group)
            {
                auto& replaced  = m_Impl->scripts[member];
                replaced.loaded = std::ranges::find(sources, replaced.path, &Impl::SourceStamp::path)->stamp;
                replaced.attempted.reset();
                count += replaced.provider ? 0 : 1;
            }
            if (std::ranges::any_of(group,
                                    [&](size_t member)
                                    {
                                        return m_Impl->scripts[member].path == m_Impl->reloadErrorPath;
                                    }))
            {
                m_Impl->reloadError.clear();
                m_Impl->reloadErrorPath.clear();
            }
            Logger::app().info("Reloaded module: {}", slot.path.string());
            try
            {
                for (const auto member : group)
                {
                    auto& replaced = m_Impl->scripts[member];
                    if (replaced.module)
                    {
                        replaced.instance->notify(VULTRA_SCENE_RELOADED);
                    }
                }
            }
            catch (const std::exception& error)
            {
                // Activation is execution of committed code, not a staging failure; stop and report that boundary.
                m_Impl->scene.stop();
                m_Impl->reloadErrorPath = slot.path;
                m_Impl->reloadError     = "Activate reloaded " + slot.path.string() + ": " + error.what();
                Logger::app().error("{}", m_Impl->reloadError);
                throw;
            }
        }
        return count;
    }

    std::string_view ScriptHost::lastReloadError() const
    {
        return m_Impl->reloadError;
    }

    const ScriptClassInfo& ScriptHost::scriptClass(ObjectId node) const
    {
        return m_Impl->scriptSlot(node).instance->scriptClass();
    }

    PropertyValue ScriptHost::readExport(ObjectId node, std::string_view name) const
    {
        const auto& slot  = m_Impl->scriptSlot(node);
        auto        value = slot.instance->readExport(name);
        m_Impl->scene.validatePropertyValue(slot.instance->scriptClass().property(name), value, m_Impl->project);
        return value;
    }

    void ScriptHost::writeExport(ObjectId node, std::string_view name, const PropertyValue& value)
    {
        auto& slot = m_Impl->scriptSlot(node);
        m_Impl->scene.validatePropertyValue(slot.instance->scriptClass().property(name), value, m_Impl->project);
        slot.instance->writeExport(name, value);
    }

    void ScriptHost::update(float deltaSeconds)
    {
        update(deltaSeconds, {});
    }

    void ScriptHost::update(float deltaSeconds, std::span<InputEvent> events, InputCapture capture)
    {
        if (m_Impl->stopped)
        {
            throw std::logic_error("Update stopped script host");
        }
        m_Impl->retireMissingNodes();
        if (m_Impl->hotReload)
        {
            reloadChanged();
        }
        m_Impl->scene.advance(deltaSeconds, events, capture);
        m_Impl->retireMissingNodes();
        if (!m_Impl->scene.running())
        {
            return;
        }
        for (auto& slot : m_Impl->scripts)
        {
            if (!slot.instance || slot.module)
            {
                continue;
            }
            slot.instance->update(deltaSeconds);
            m_Impl->retireMissingNodes();
        }
    }

    void ScriptHost::gui(EditorGui& gui)
    {
        if (m_Impl->stopped)
        {
            throw std::logic_error("Draw stopped script host");
        }
#ifdef VULTRA_SCRIPT_GUI
        m_Impl->retireMissingNodes();
        for (const auto& slot : m_Impl->scripts)
        {
            if (!slot.instance)
            {
                continue;
            }
            const auto* node = slot.module ? m_Impl->scene.find(slot.target) : nullptr;
            if (slot.instance && (!slot.module || (node && node->isInsideTree() && !node->isQueuedForDeletion())))
            {
                slot.instance->gui(gui);
            }
        }
#else
        throw std::logic_error("Script GUI requires the editor_gui capability");
#endif
    }

    void ScriptHost::stop() noexcept
    {
        if (m_Impl->stopped)
        {
            return;
        }
        m_Impl->stopped = true;
        m_Impl->scene.stop();
        for (auto it = m_Impl->scripts.rbegin(); it != m_Impl->scripts.rend(); ++it)
        {
            if (it->instance)
            {
                if (it->module)
                {
                    if (auto* node = m_Impl->scene.find(it->target))
                    {
                        m_Impl->scene.detachBehavior(*node, *it->instance);
                    }
                }
                it->instance->stop();
            }
        }
        m_Impl->scripts.clear();
#ifdef VULTRA_SCRIPT_LUA
        m_Impl->lua.reset();
#endif
    }
} // namespace vultra
