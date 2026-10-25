#include "lua_context.hpp"
#include "script_module.hpp"

#include <vultra/api/script_property_callbacks.generated.hpp>
#ifdef VULTRA_SCRIPT_GUI
#include <vultra/api/ui_bridge.hpp>
#include <vultra/ui/editor_gui.hpp>
#endif
#include <vultra/scripting/script_property_json.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace vultra
{
    namespace
    {
        struct LuaSceneScope
        {
            SceneAccess&     access;
            LuaCallbackScope callback;

            static VultraSceneFrame activate(SceneAccess& access)
            {
                if (access.active)
                {
                    throw std::logic_error("Reentrant Lua lifecycle callback");
                }
                ++access.serial;
                access.active = true;
                return {&access, access.serial};
            }

            LuaSceneScope(LuaContext& context, SceneAccess& access) :
                access(access),
                callback(context, activate(access))
            {
            }

            ~LuaSceneScope()
            {
                access.active = false;
            }

            LuaSceneScope(const LuaSceneScope&)            = delete;
            LuaSceneScope& operator=(const LuaSceneScope&) = delete;
        };

        class LuaScript final : public ScriptInstance
        {
        public:
            LuaScript(const std::filesystem::path& path, LuaContext& context, ObjectId node) :
                m_Context(context),
                m_State(context.state()),
                m_Path(path)
            {
                LuaStackScope stack(m_State);
                m_Access.scene   = &context.scene();
                m_Access.project = context.project();
                try
                {
                    m_ScriptRef = context.instantiateScript(path);
                    initializeExports(path);
                    bindProperties(&scripting::scriptPropertyApi<LuaScript>(),
                                   this,
                                   context.scene(),
                                   context.project());
                    context.attachScript(m_ScriptRef, node);
                }
                catch (...)
                {
                    stop();
                    throw;
                }
            }

            ~LuaScript() override
            {
                stop();
            }

            std::string describePropertiesJson() const
            {
                return m_PropertyDescription;
            }

            std::string readExportJson(VultraSceneFrame frame, std::string_view name) const
            {
                LuaCallbackScope callback(m_Context, frame);
                LuaStackScope    scope(m_State);
                lua_rawgeti(m_State, LUA_REGISTRYINDEX, m_ScriptRef);
                lua_pushlstring(m_State, name.data(), name.size());
                lua_rawget(m_State, -2);
                return m_Context.readValue(scriptClass().property(name), m_State, -1).dump();
            }

            void writeExportJson(VultraSceneFrame frame, std::string_view name, std::string_view json)
            {
                LuaCallbackScope callback(m_Context, frame);
                const auto       value = nlohmann::json::parse(json);
                LuaStackScope    scope(m_State);
                lua_rawgeti(m_State, LUA_REGISTRYINDEX, m_ScriptRef);
                lua_pushlstring(m_State, name.data(), name.size());
                m_Context.pushValue(scriptClass().property(name), m_State, value);
                lua_rawset(m_State, -3);
            }

            static nlohmann::json declarationMap(lua_State* state, int index, bool allowClassReference = false)
            {
                LuaStackScope stack(state);
                if (!lua_istable(state, index))
                {
                    throw std::invalid_argument("Export metadata requires a table");
                }
                index       = lua_absindex(state, index);
                auto result = nlohmann::json::object();
                lua_pushnil(state);
                while (lua_next(state, index))
                {
                    const auto key = readLuaProperty(state, -2).get<std::string>();
                    result[key] =
                        allowClassReference && key == "class" ? declarationMap(state, -1) : readLuaProperty(state, -1);
                    lua_pop(state, 1);
                }
                return result;
            }

            void initializeExports(const std::filesystem::path& path)
            {
                using Json = nlohmann::json;
                LuaStackScope scope(m_State);
                lua_rawgeti(m_State, LUA_REGISTRYINDEX, m_ScriptRef);
                const int script = lua_gettop(m_State);
                // Declarations are data; metadata collection must not execute a class's __index callback.
                lua_getmetatable(m_State, script);
                lua_pushliteral(m_State, "__vultra_classes");
                lua_rawget(m_State, -2);
                const int classes = lua_gettop(m_State);
                Json      description {{"version", 1},
                                       {"category", "script"},
                                       {"name", ""},
                                       {"base", "Node"},
                                       {"classes", Json::array()},
                                       {"dependencies", m_Context.dependencies(path)},
                                       {"callbacks", Json::array()},
                                       {"properties", Json::array()}};
                for (size_t index = 1; index <= lua_rawlen(m_State, classes); ++index)
                {
                    lua_rawgeti(m_State, classes, lua_Integer(index));
                    const int declaration = lua_gettop(m_State);
                    for (const char* callback : {"_input", "_unhandled_input"})
                    {
                        lua_pushstring(m_State, callback);
                        lua_rawget(m_State, declaration);
                        if (!lua_isnil(m_State, -1))
                        {
                            if (!lua_isfunction(m_State, -1))
                            {
                                throw std::invalid_argument(std::string("Lua input callback is not a function: ") +
                                                            callback);
                            }
                            auto& callbacks = description["callbacks"];
                            if (std::find(callbacks.begin(), callbacks.end(), callback + 1) == callbacks.end())
                            {
                                callbacks.push_back(callback + 1);
                            }
                        }
                        lua_pop(m_State, 1);
                    }
                    auto source = m_Context.declarationSource(m_State, declaration);
                    if (source.empty())
                    {
                        source = path.generic_string();
                    }
                    auto text = [&](const char* name, std::string fallback)
                    {
                        lua_pushstring(m_State, name);
                        lua_rawget(m_State, declaration);
                        if (!lua_isnil(m_State, -1))
                        {
                            fallback = readLuaProperty(m_State, -1).get<std::string>();
                        }
                        lua_pop(m_State, 1);
                        return fallback;
                    };
                    const auto className = text("class_name", std::filesystem::path(source).stem().string());
                    description["classes"].push_back({{"module", index == 1 ? "" : source}, {"name", className}});
                    if (index == 1)
                    {
                        description["name"] = className;
                    }
                    if (index == lua_rawlen(m_State, classes))
                    {
                        description["base"] = text("extends", "Node");
                    }
                    lua_pushliteral(m_State, "exports");
                    lua_rawget(m_State, declaration);
                    if (!lua_isnil(m_State, -1))
                    {
                        if (!lua_istable(m_State, -1))
                        {
                            throw std::invalid_argument("Lua exports must be a table");
                        }
                        const int exports = lua_gettop(m_State);
                        lua_pushnil(m_State);
                        while (lua_next(m_State, exports))
                        {
                            if (lua_type(m_State, -2) != LUA_TSTRING || !lua_istable(m_State, -1))
                            {
                                throw std::invalid_argument("Lua exports require named property definitions");
                            }
                            const auto name = readLuaProperty(m_State, -2).get<std::string>();
                            for (const auto& inherited : description["properties"])
                            {
                                if (inherited.at("name") == name)
                                {
                                    throw std::invalid_argument("Lua script field shadows an inherited export: " +
                                                                name);
                                }
                            }
                            if (name == "node" || name == "scene" || name == "exports" || name == "class_name" ||
                                name == "extends" || name == "_enter_tree" || name == "_ready" || name == "_process" ||
                                name == "_physics_process" || name == "_exit_tree" || name == "_editor_gui")
                            {
                                throw std::invalid_argument("Lua export shadows a reserved script member: " + name);
                            }
                            const int definition = lua_gettop(m_State);
                            Json      options    = Json::object();
                            lua_pushnil(m_State);
                            while (lua_next(m_State, definition))
                            {
                                const auto key = readLuaProperty(m_State, -2).get<std::string>();
                                if (key != "kind" && key != "default" && key != "label" && key != "min" &&
                                    key != "max" && key != "speed" && key != "widget" && key != "integer_bits" &&
                                    key != "element_kind" && key != "enum" && key != "reference")
                                {
                                    throw std::invalid_argument("Unknown Lua export option: " + key);
                                }
                                options[key] = key == "enum" || key == "reference" ?
                                                   declarationMap(m_State, -1, key == "reference") :
                                                   readLuaProperty(m_State, -1);
                                lua_pop(m_State, 1);
                            }
                            const auto kind   = options.at("kind").get<std::string>();
                            const auto scalar = kind == "array" ? options.at("element_kind").get<std::string>() : kind;
                            if (scalar != "bool" && scalar != "int" && scalar != "uint" && scalar != "float" &&
                                scalar != "double" && scalar != "string" && scalar != "vector2" &&
                                scalar != "vector3" && scalar != "vector4" && scalar != "matrix4" && scalar != "enum" &&
                                scalar != "reference")
                            {
                                throw std::invalid_argument("Unsupported Lua export kind: " + scalar);
                            }
                            if (scalar == "reference" && kind != "array" && !options.contains("default"))
                            {
                                options["default"] = nullptr;
                            }
                            const uint8_t bits = scalar == "int" || scalar == "uint" ? 64 : 0;
                            auto          property =
                                scripting::scriptPropertyDescription(name, kind, bits, options.at("default"));
                            if (kind == "array")
                            {
                                property["element_kind"] = scalar;
                            }
                            if (scalar == "reference")
                            {
                                property["reference"] = options.at("reference");
                                if (!property["reference"].contains("nullable"))
                                {
                                    property["reference"]["nullable"] = true;
                                }
                                auto& reference = property["reference"];
                                if (reference.contains("class"))
                                {
                                    const auto  module = reference["class"].value("module", std::string {});
                                    std::string origin;
                                    if (!module.empty())
                                    {
                                        origin = std::filesystem::canonical(
                                                     std::filesystem::path(source).parent_path() / module)
                                                     .generic_string();
                                    }
                                    else if (index != 1)
                                    {
                                        origin = source;
                                    }
                                    reference["class"]["module"] = std::move(origin);
                                }
                            }
                            else if (options.contains("reference"))
                            {
                                throw std::invalid_argument("Only reference exports accept a target type");
                            }
                            if (scalar == "enum")
                            {
                                std::vector<Json> choices;
                                for (const auto& [label, value] : options.at("enum").items())
                                {
                                    if (!value.is_number_integer() ||
                                        (value.is_number_unsigned() && value.get<uint64_t>() > uint64_t(INT64_MAX)))
                                    {
                                        throw std::invalid_argument("Enum choices require signed 64-bit integers");
                                    }
                                    choices.push_back({{"label", label}, {"value", value}});
                                }
                                std::ranges::sort(choices,
                                                  [](const Json& left, const Json& right)
                                                  {
                                                      const auto a = left.at("value").get<int64_t>();
                                                      const auto b = right.at("value").get<int64_t>();
                                                      return a == b ? left.at("label").get<std::string>() <
                                                                          right.at("label").get<std::string>() :
                                                                      a < b;
                                                  });
                                for (const auto& choice : choices)
                                {
                                    property["choices"].push_back(choice);
                                    auto& values = property["enum_values"];
                                    if (std::find(values.begin(), values.end(), choice.at("value")) == values.end())
                                    {
                                        values.push_back(choice.at("value"));
                                    }
                                }
                            }
                            else if (options.contains("enum"))
                            {
                                throw std::invalid_argument("Only enum exports accept choices");
                            }
                            for (const auto* key : {"label", "speed", "widget", "integer_bits"})
                            {
                                if (options.contains(key))
                                {
                                    property[key] = options[key];
                                }
                            }
                            if (options.contains("min") || options.contains("max"))
                            {
                                property["range_hint"] = {{"min", options.value("min", 0.0)},
                                                          {"max", options.value("max", 0.0)}};
                            }
                            // Sort below: Lua table iteration order must not determine the public class schema.
                            description["properties"].push_back(std::move(property));
                            lua_getmetatable(m_State, script);
                            lua_getfield(m_State, -1, "__vultra_exports");
                            lua_pushlstring(m_State, name.data(), name.size());
                            lua_pushboolean(m_State, true);
                            lua_rawset(m_State, -3);
                            lua_pop(m_State, 2);
                            lua_pushlstring(m_State, name.data(), name.size());
                            pushLuaProperty(m_State, options.at("default"));
                            lua_rawset(m_State, script);
                            lua_pop(m_State, 1);
                        }
                    }
                    lua_pop(m_State, 2);
                }
                auto& properties = description["properties"].get_ref<Json::array_t&>();
                std::ranges::sort(properties,
                                  {},
                                  [](const Json& property)
                                  {
                                      return property.at("name").get<std::string>();
                                  });
                m_PropertyDescription = description.dump();
            }

            void update(float deltaSeconds) override
            {
                LuaSceneScope scope(m_Context, m_Access);
                call("_process", deltaSeconds);
            }

            void gui(EditorGui& gui) override
            {
#ifdef VULTRA_SCRIPT_GUI
                if (!gui.frameActive())
                {
                    throw std::logic_error("Lua UI requires an active GUI frame");
                }
                LuaCallbackScope scope(m_Context, {}, makeUiFrame(gui));
                call("_editor_gui", std::nullopt, true);
#else
                throw std::logic_error("Lua UI requires the editor_gui capability");
#endif
            }

            void notify(VultraSceneEvent event, float deltaSeconds) override
            {
                LuaSceneScope scope(m_Context, m_Access);
                switch (event)
                {
                    case VULTRA_SCENE_ENTER_TREE:
                        call("_enter_tree");
                        break;
                    case VULTRA_SCENE_READY:
                        call("_ready");
                        break;
                    case VULTRA_SCENE_PHYSICS_PROCESS:
                        call("_physics_process", deltaSeconds);
                        break;
                    case VULTRA_SCENE_EXIT_TREE:
                        call("_exit_tree");
                        break;
                    case VULTRA_SCENE_RELOADED:
                        break;
                    case VULTRA_SCENE_INPUT:
                        call("_input", {}, false, true);
                        break;
                    case VULTRA_SCENE_UNHANDLED_INPUT:
                        call("_unhandled_input", {}, false, true);
                        break;
                    default:
                        throw std::invalid_argument("Unknown Lua scene event");
                }
            }

            void stop() noexcept override
            {
                if (m_State)
                {
                    if (m_ScriptRef != LUA_NOREF)
                    {
                        m_Context.releaseScript(m_ScriptRef, m_Path);
                    }
                    m_ScriptRef     = LUA_NOREF;
                    m_State         = nullptr;
                    m_Access.active = false;
                }
            }

        private:
            static int dispatch(lua_State* state)
            {
                const auto* name = lua_tostring(state, 2);
                // Class-table lookup can invoke a user __index; keep it inside the protected call as well.
                lua_getfield(state, 1, name);
                if (lua_isnil(state, -1))
                {
                    return 0;
                }
                if (!lua_isfunction(state, -1))
                {
                    return luaL_error(state, "Lua callback is not a function: %s", name);
                }
                lua_pushvalue(state, 1);
                int arguments = 1;
                if (!lua_isnil(state, 3))
                {
                    lua_pushvalue(state, 3);
                    ++arguments;
                }
                if (!lua_isnil(state, 4))
                {
                    lua_pushvalue(state, 4);
                    ++arguments;
                }
                lua_call(state, arguments, 0);
                return 0;
            }

            void call(const char* name, std::optional<float> delta = {}, bool withGui = false, bool withInput = false)
            {
                LuaStackScope stack(m_State);
                lua_pushcfunction(m_State, dispatch);
                lua_rawgeti(m_State, LUA_REGISTRYINDEX, m_ScriptRef);
                lua_pushstring(m_State, name);
                if (withInput)
                {
                    m_Context.pushInputEvent();
                }
                else if (delta)
                {
                    lua_pushnumber(m_State, *delta);
                }
                else
                {
                    lua_pushnil(m_State);
                }
                if (withGui)
                {
                    m_Context.pushEditorGui();
                }
                else
                {
                    lua_pushnil(m_State);
                }
                if (lua_pcall(m_State, 4, 0, 0) != LUA_OK)
                {
                    const char* message = lua_tostring(m_State, -1);
                    throw std::runtime_error(std::string("Lua ") + name + ": " + (message ? message : "unknown error"));
                }
            }

            LuaContext&           m_Context;
            lua_State*            m_State;
            std::filesystem::path m_Path;
            int                   m_ScriptRef = LUA_NOREF;
            std::string           m_PropertyDescription;
            SceneAccess           m_Access;
        };
    } // namespace

    std::unique_ptr<ScriptInstance> loadLuaScript(const std::filesystem::path& path, LuaContext& context, ObjectId node)
    {
        return std::make_unique<LuaScript>(path, context, node);
    }
} // namespace vultra
