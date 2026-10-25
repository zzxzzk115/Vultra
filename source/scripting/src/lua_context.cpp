#include "lua_context.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace vultra
{
    LuaContext::LuaContext(SceneTree& scene, const ProjectManifest* project) :
        m_Scene(scene),
        m_Project(project),
        m_State(luaL_newstate())
    {
        if (!m_State)
        {
            throw std::runtime_error("Create Lua world state");
        }
        try
        {
            initializeApi();
            initializeModules();
        }
        catch (...)
        {
            lua_close(m_State);
            throw;
        }
    }

    LuaContext::~LuaContext()
    {
        m_SceneFrame = {};
        m_UiFrame    = {};
        lua_close(m_State);
    }

    lua_State* LuaContext::state() const
    {
        return m_State;
    }

    SceneTree& LuaContext::scene() const
    {
        return m_Scene;
    }

    const ProjectManifest* LuaContext::project() const
    {
        return m_Project;
    }

    void LuaContext::bind(const char* name, lua_CFunction function)
    {
        lua_pushlightuserdata(m_State, this);
        lua_pushcclosure(m_State, function, 1);
        lua_setfield(m_State, -2, name);
    }

    int LuaContext::instantiateScript(const std::filesystem::path& path)
    {
        LuaCallbackScope callback(*this);
        LuaStackScope    stack(m_State);
        const auto       name = path.string();
        if (luaL_loadfile(m_State, name.c_str()) != LUA_OK)
        {
            throw std::runtime_error("Load Lua script " + name + ": " + lua_tostring(m_State, -1));
        }
        const int chunk = lua_gettop(m_State);
        // Ordinary globals belong to this instance. Required modules deliberately share the world's package cache.
        lua_newtable(m_State);
        lua_pushvalue(m_State, -1);
        lua_setfield(m_State, -2, "_G");
        lua_newtable(m_State);
        lua_pushglobaltable(m_State);
        lua_setfield(m_State, -2, "__index");
        lua_setmetatable(m_State, -2);
        lua_setupvalue(m_State, chunk, 1);
        auto previousRoot = std::exchange(m_CurrentRoot, path);
        int  status       = LUA_OK;
        try
        {
            if (m_Reloading && m_ReloadRoots.insert(path).second)
            {
                m_RootModules[path].clear();
            }
            status = lua_pcall(m_State, 0, 1, 0);
        }
        catch (...)
        {
            m_CurrentRoot = std::move(previousRoot);
            throw;
        }
        m_CurrentRoot = std::move(previousRoot);
        if (status != LUA_OK)
        {
            throw std::runtime_error("Load Lua script " + name + ": " + lua_tostring(m_State, -1));
        }
        if (!lua_istable(m_State, -1))
        {
            throw std::invalid_argument("Lua script must return a class table: " + name);
        }
        // A require() result may be shared. Export values and the node/scene attachment must be instance-owned.
        const int declaration = lua_gettop(m_State);
        if (declarationSource(m_State, declaration).empty())
        {
            rememberDeclaration(m_State, declaration, path);
        }
        lua_newtable(m_State);
        const int classes = lua_gettop(m_State);
        lua_pushvalue(m_State, declaration);
        std::set<const void*> visited;
        while (lua_istable(m_State, -1))
        {
            if (!visited.insert(lua_topointer(m_State, -1)).second)
            {
                throw std::invalid_argument("Lua script inheritance contains a cycle: " + name);
            }
            lua_pushvalue(m_State, -1);
            lua_rawseti(m_State, classes, lua_Integer(visited.size()));
            lua_pushliteral(m_State, "extends");
            lua_rawget(m_State, -2);
            lua_remove(m_State, -2);
        }
        if (!lua_isnil(m_State, -1) && lua_type(m_State, -1) != LUA_TSTRING)
        {
            throw std::invalid_argument("Lua extends requires a native base name or a script class table: " + name);
        }
        lua_pop(m_State, 1);
        lua_newtable(m_State);
        lua_newtable(m_State);
        lua_pushvalue(m_State, classes);
        lua_setfield(m_State, -2, "__vultra_classes");
        lua_newtable(m_State);
        lua_setfield(m_State, -2, "__vultra_exports");
        lua_pushvalue(m_State, classes);
        lua_getfield(m_State, -2, "__vultra_exports");
        lua_pushcclosure(m_State, instanceIndex, 2);
        lua_setfield(m_State, -2, "__index");
        lua_setmetatable(m_State, -2);
        ++m_RootUsers[path];
        return luaL_ref(m_State, LUA_REGISTRYINDEX);
    }

    void LuaContext::releaseScript(int reference, const std::filesystem::path& path)
    {
        luaL_unref(m_State, LUA_REGISTRYINDEX, reference);
        const auto found = m_RootUsers.find(path);
        if (found != m_RootUsers.end() && --found->second == 0)
        {
            m_RootUsers.erase(found);
            m_RootModules.erase(path);
        }
    }

    int LuaContext::instanceIndex(lua_State* state)
    {
        lua_pushvalue(state, 2);
        lua_rawget(state, lua_upvalueindex(2));
        if (lua_toboolean(state, -1))
        {
            return 0;
        }
        lua_pop(state, 1);
        for (size_t index = 1; index <= lua_rawlen(state, lua_upvalueindex(1)); ++index)
        {
            lua_rawgeti(state, lua_upvalueindex(1), lua_Integer(index));
            lua_pushvalue(state, 2);
            lua_gettable(state, -2);
            if (!lua_isnil(state, -1))
            {
                return 1;
            }
            lua_pop(state, 2);
        }
        return 0;
    }

    void LuaContext::attachScript(int reference, ObjectId node)
    {
        LuaStackScope stack(m_State);
        lua_rawgeti(m_State, LUA_REGISTRYINDEX, m_AttachRef);
        lua_rawgeti(m_State, LUA_REGISTRYINDEX, reference);
        lua_pushinteger(m_State, static_cast<lua_Integer>(node.value));
        if (lua_pcall(m_State, 2, 0, 0) != LUA_OK)
        {
            throw std::runtime_error(std::string("Attach Lua script: ") + lua_tostring(m_State, -1));
        }
    }

    void LuaContext::pushEditorGui()
    {
        lua_rawgeti(m_State, LUA_REGISTRYINDEX, m_GuiRef);
    }

    LuaCallbackScope::LuaCallbackScope(LuaContext& context, VultraSceneFrame scene, VultraUiFrame ui) :
        m_Context(context),
        m_PreviousScene(context.m_SceneFrame),
        m_PreviousUi(context.m_UiFrame)
    {
        context.m_SceneFrame = scene;
        context.m_UiFrame    = ui;
    }

    LuaCallbackScope::~LuaCallbackScope()
    {
        m_Context.m_SceneFrame = m_PreviousScene;
        m_Context.m_UiFrame    = m_PreviousUi;
    }

    nlohmann::json LuaContext::readReference(const PropertySchema& property, lua_State* state, int index) const
    {
        if (lua_isnil(state, index) || (lua_islightuserdata(state, index) && !lua_touserdata(state, index)))
        {
            return nullptr;
        }
        if (!lua_istable(state, index))
        {
            throw std::invalid_argument("Reference requires a Node/Resource view or nil");
        }
        LuaStackScope stack(state);
        index = lua_absindex(state, index);
        lua_pushliteral(state, "id");
        lua_rawget(state, index);
        int        valid = 0;
        const auto id    = uint64_t(lua_tointegerx(state, -1, &valid));
        if (!valid || id == 0)
        {
            throw std::invalid_argument("Reference requires a runtime object identity");
        }
        const auto& type = property.referenceType;
        uint64_t    size = 0;
        auto status = sceneApi().object_persistent_id(m_SceneFrame, id, type.data(), type.size(), nullptr, 0, &size);
        if (status != VULTRA_STATUS_OK)
        {
            throw std::invalid_argument("Read reference identity: API status " + std::to_string(int(status)));
        }
        std::string result(size, '\0');
        status =
            sceneApi()
                .object_persistent_id(m_SceneFrame, id, type.data(), type.size(), result.data(), result.size(), &size);
        if (status != VULTRA_STATUS_OK)
        {
            throw std::invalid_argument("Read reference identity: API status " + std::to_string(int(status)));
        }
        return result;
    }

    void LuaContext::pushReference(const PropertySchema& property,
                                   lua_State*            state,
                                   const nlohmann::json& value,
                                   bool                  arrayElement) const
    {
        if (value.is_null())
        {
            pushLuaProperty(state, value, arrayElement);
            return;
        }
        const auto identity = value.get<std::string>();
        const bool node     = property.referenceCategory == "node";
        const auto find     = node ? sceneApi().find_node : sceneApi().find_resource;
        uint64_t   id       = 0;
        const auto status   = find(m_SceneFrame, identity.data(), identity.size(), &id);
        if (status != VULTRA_STATUS_OK || id == 0)
        {
            throw std::invalid_argument("Reference target is not loaded in this world");
        }
        lua_rawgeti(state, LUA_REGISTRYINDEX, m_WrapRef);
        lua_pushinteger(state, static_cast<lua_Integer>(id));
        lua_pushboolean(state, node);
        if (lua_pcall(state, 2, 1, 0) != LUA_OK)
        {
            const auto* error = lua_tostring(state, -1);
            throw std::invalid_argument(std::string("Wrap reference: ") + (error ? error : "unknown Lua error"));
        }
    }

    nlohmann::json LuaContext::readValue(const PropertySchema& property, lua_State* state, int index) const
    {
        if (property.kind == PropertyKind::eReference)
        {
            return readReference(property, state, index);
        }
        if (property.kind != PropertyKind::eArray || property.elementKind != PropertyKind::eReference)
        {
            return readLuaProperty(state, index);
        }
        LuaStackScope stack(state);
        if (!lua_istable(state, index))
        {
            throw std::invalid_argument("Reference array requires a table");
        }
        index            = lua_absindex(state, index);
        const auto count = lua_rawlen(state, index);
        size_t     keys  = 0;
        lua_pushnil(state);
        while (lua_next(state, index))
        {
            const auto key = lua_isinteger(state, -2) ? lua_tointeger(state, -2) : 0;
            if (key < 1 || uint64_t(key) > count)
            {
                throw std::invalid_argument("Reference arrays require consecutive indices");
            }
            ++keys;
            lua_pop(state, 1);
        }
        if (keys != count)
        {
            throw std::invalid_argument("Reference arrays cannot contain holes; use scene.null for empty elements");
        }
        auto result = nlohmann::json::array();
        for (size_t element = 1; element <= count; ++element)
        {
            lua_rawgeti(state, index, lua_Integer(element));
            result.push_back(readReference(property, state, -1));
            lua_pop(state, 1);
        }
        return result;
    }

    void LuaContext::pushValue(const PropertySchema& property, lua_State* state, const nlohmann::json& value) const
    {
        if (property.kind == PropertyKind::eReference)
        {
            pushReference(property, state, value, false);
        }
        else if (property.kind == PropertyKind::eArray && property.elementKind == PropertyKind::eReference)
        {
            if (!value.is_array())
            {
                throw std::invalid_argument("Reference property requires an array");
            }
            lua_createtable(state, int(value.size()), 0);
            for (size_t index = 0; index < value.size(); ++index)
            {
                pushReference(property, state, value[index], true);
                lua_rawseti(state, -2, lua_Integer(index + 1));
            }
        }
        else
        {
            pushLuaProperty(state, value);
        }
    }

    void pushLuaProperty(lua_State* state, const nlohmann::json& value, bool arrayElement)
    {
        if (value.is_null())
        {
            if (arrayElement)
            {
                lua_pushlightuserdata(state, nullptr);
            }
            else
            {
                lua_pushnil(state);
            }
        }
        else if (value.is_boolean())
        {
            lua_pushboolean(state, value.get<bool>());
        }
        else if (value.is_number_integer())
        {
            if (value.is_number_unsigned() && value.get<uint64_t>() > uint64_t(INT64_MAX))
            {
                throw std::invalid_argument("Property exceeds Lua's signed integer range");
            }
            lua_pushinteger(state, value.get<int64_t>());
        }
        else if (value.is_number())
        {
            lua_pushnumber(state, value.get<double>());
        }
        else if (value.is_string())
        {
            const auto& text = value.get_ref<const std::string&>();
            lua_pushlstring(state, text.data(), text.size());
        }
        else if (value.is_array())
        {
            lua_createtable(state, int(value.size()), 0);
            for (size_t index = 0; index < value.size(); ++index)
            {
                pushLuaProperty(state, value[index], true);
                lua_rawseti(state, -2, lua_Integer(index + 1));
            }
        }
        else if (value.is_object())
        {
            lua_createtable(state, 0, int(value.size()));
            for (const auto& [key, item] : value.items())
            {
                lua_pushlstring(state, key.data(), key.size());
                pushLuaProperty(state, item);
                lua_rawset(state, -3);
            }
        }
        else
        {
            throw std::invalid_argument("Unsupported Lua property value");
        }
    }

    nlohmann::json readLuaProperty(lua_State* state, int index, int depth)
    {
        switch (lua_type(state, index))
        {
            case LUA_TNIL:
                return nullptr;
            case LUA_TBOOLEAN:
                return lua_toboolean(state, index) != 0;
            case LUA_TNUMBER:
                if (lua_isinteger(state, index))
                {
                    return lua_tointeger(state, index);
                }
                if (!std::isfinite(lua_tonumber(state, index)))
                {
                    throw std::invalid_argument("Property requires finite numbers");
                }
                return lua_tonumber(state, index);
            case LUA_TLIGHTUSERDATA:
                if (!lua_touserdata(state, index))
                {
                    return nullptr;
                }
                throw std::invalid_argument("Invalid Lua property sentinel");
            case LUA_TSTRING: {
                size_t      size = 0;
                const auto* text = lua_tolstring(state, index, &size);
                return std::string(text, size);
            }
            case LUA_TTABLE: {
                if (depth >= 2)
                {
                    throw std::invalid_argument("Property tables must be scalar/vector arrays");
                }
                index           = lua_absindex(state, index);
                const auto size = lua_rawlen(state, index);
                size_t     keys = 0;
                lua_pushnil(state);
                while (lua_next(state, index))
                {
                    const auto key = lua_isinteger(state, -2) ? lua_tointeger(state, -2) : 0;
                    if (key < 1 || uint64_t(key) > size)
                    {
                        throw std::invalid_argument("Property tables require consecutive array indices");
                    }
                    ++keys;
                    lua_pop(state, 1);
                }
                if (keys != size)
                {
                    throw std::invalid_argument("Property arrays cannot contain holes");
                }
                auto result = nlohmann::json::array();
                for (size_t item = 1; item <= size; ++item)
                {
                    lua_rawgeti(state, index, lua_Integer(item));
                    result.push_back(readLuaProperty(state, -1, depth + 1));
                    lua_pop(state, 1);
                }
                return result;
            }
            default:
                throw std::invalid_argument("Property requires a scalar, UUID or array");
        }
    }

} // namespace vultra
