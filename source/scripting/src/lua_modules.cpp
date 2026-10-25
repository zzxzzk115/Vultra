#include "lua_context.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace vultra
{
    namespace
    {
        void copyTable(lua_State* state, int source, int destination)
        {
            source      = lua_absindex(state, source);
            destination = lua_absindex(state, destination);
            lua_pushnil(state);
            while (lua_next(state, source))
            {
                lua_pushvalue(state, -2);
                lua_pushvalue(state, -2);
                lua_rawset(state, destination);
                lua_pop(state, 1);
            }
        }

        void clearTable(lua_State* state, int table)
        {
            table = lua_absindex(state, table);
            lua_pushnil(state);
            while (lua_next(state, table))
            {
                lua_pop(state, 1);
                lua_pushvalue(state, -1);
                lua_pushnil(state);
                lua_rawset(state, table);
            }
        }
    } // namespace

    void LuaContext::initializeModules()
    {
        lua_getglobal(m_State, "require");
        m_RequireRef = luaL_ref(m_State, LUA_REGISTRYINDEX);
        lua_pushlightuserdata(m_State, this);
        lua_pushcclosure(m_State, requireModule, 1);
        lua_setglobal(m_State, "require");
        // Origins must not keep retired class tables alive after a failed or successful replacement.
        lua_newtable(m_State);
        lua_newtable(m_State);
        lua_pushliteral(m_State, "k");
        lua_setfield(m_State, -2, "__mode");
        lua_setmetatable(m_State, -2);
        m_OriginsRef = luaL_ref(m_State, LUA_REGISTRYINDEX);
    }

    void LuaContext::setModuleRoot(const std::filesystem::path& root)
    {
        const auto canonical = std::filesystem::canonical(root);
        if (!m_ModuleRoot.empty())
        {
            if (m_ModuleRoot != canonical)
            {
                throw std::invalid_argument("A Lua host cannot mix project module roots");
            }
            return;
        }
        LuaStackScope stack(m_State);
        lua_getglobal(m_State, "package");
        lua_getfield(m_State, -1, "path");
        if (lua_type(m_State, -1) != LUA_TSTRING)
        {
            throw std::invalid_argument("Lua package.path must be a string");
        }
        const auto path = (canonical / "?.lua").generic_string() + ";" + (canonical / "?/init.lua").generic_string() +
                          ";" + lua_tostring(m_State, -1);
        lua_pop(m_State, 1);
        lua_pushlstring(m_State, path.data(), path.size());
        lua_setfield(m_State, -2, "path");
        m_ModuleRoot = canonical;
    }

    void LuaContext::rememberDeclaration(lua_State* state, int index, const std::filesystem::path& source)
    {
        index           = lua_absindex(state, index);
        const auto path = source.generic_string();
        lua_rawgeti(state, LUA_REGISTRYINDEX, m_OriginsRef);
        lua_pushvalue(state, index);
        lua_pushlstring(state, path.data(), path.size());
        lua_rawset(state, -3);
        lua_pop(state, 1);
    }

    std::string LuaContext::declarationSource(lua_State* state, int index) const
    {
        LuaStackScope stack(state);
        index = lua_absindex(state, index);
        lua_rawgeti(state, LUA_REGISTRYINDEX, m_OriginsRef);
        lua_pushvalue(state, index);
        lua_rawget(state, -2);
        if (lua_type(state, -1) != LUA_TSTRING)
        {
            return {};
        }
        size_t      size = 0;
        const auto* text = lua_tolstring(state, -1, &size);
        return {text, size};
    }

    int LuaContext::requireModule(lua_State* state)
    {
        auto* context = static_cast<LuaContext*>(lua_touserdata(state, lua_upvalueindex(1)));
        try
        {
            return context->require(state);
        }
        catch (const std::exception& error)
        {
            lua_pushstring(state, error.what());
        }
        // Leave the C++ exception handler before Lua performs its non-local error jump.
        return lua_error(state);
    }

    int LuaContext::require(lua_State* state)
    {
        size_t            size = 0;
        const auto*       data = luaL_checklstring(state, 1, &size);
        const std::string name(data, size);
        if (name.find('\0') != std::string::npos)
        {
            throw std::invalid_argument("Lua module name contains a NUL byte");
        }
        if (m_ReloadScope)
        {
            m_ReloadScope->refresh(name);
        }
        if (!m_LoadingModules.empty())
        {
            m_Modules[m_LoadingModules.back()].children.insert(name);
        }
        else if (!m_CurrentRoot.empty())
        {
            m_RootModules[m_CurrentRoot].insert(name);
        }
        const auto top = lua_gettop(state);
        lua_rawgeti(state, LUA_REGISTRYINDEX, m_RequireRef);
        lua_pushvalue(state, 1);
        m_LoadingModules.push_back(name);
        const auto status = lua_pcall(state, 1, LUA_MULTRET, 0);
        m_LoadingModules.pop_back();
        if (status != LUA_OK)
        {
            const auto* message = lua_tostring(state, -1);
            throw std::runtime_error("Require Lua module " + name + ": " + (message ? message : "non-string error"));
        }
        const auto results = lua_gettop(state) - top;
        if (results > 1 && m_ReloadScope)
        {
            m_ReloadScope->m_PendingInvalidations.erase(name);
        }
        auto& module = m_Modules[name];
        if (results > 1 && lua_type(state, top + 2) == LUA_TSTRING)
        {
            const std::filesystem::path candidate(lua_tostring(state, top + 2));
            if (candidate.extension() == ".lua" || candidate.extension() == ".luac")
            {
                module.source   = std::filesystem::canonical(candidate);
                module.modified = std::filesystem::last_write_time(module.source);
                module.size     = std::filesystem::file_size(module.source);
            }
        }
        if (results > 0 && lua_istable(state, top + 1) && !module.source.empty())
        {
            rememberDeclaration(state, top + 1, module.source);
        }
        return results;
    }

    std::set<std::string> LuaContext::moduleClosure(const Modules& modules, const std::set<std::string>& roots)
    {
        std::vector<std::string> pending(roots.begin(), roots.end());
        std::set<std::string>    visited;
        for (size_t index = 0; index < pending.size(); ++index)
        {
            if (!visited.insert(pending[index]).second)
            {
                continue;
            }
            const auto module = modules.find(pending[index]);
            if (module == modules.end())
            {
                continue;
            }
            pending.insert(pending.end(), module->second.children.begin(), module->second.children.end());
        }
        return visited;
    }

    std::vector<std::string> LuaContext::dependencies(const std::filesystem::path& root) const
    {
        const auto found = m_RootModules.find(root);
        if (found == m_RootModules.end())
        {
            return {};
        }
        std::set<std::string> paths;
        for (const auto& name : moduleClosure(m_Modules, found->second))
        {
            const auto module = m_Modules.find(name);
            if (module != m_Modules.end() && !module->second.source.empty())
            {
                paths.insert(module->second.source.generic_string());
            }
        }
        return {paths.begin(), paths.end()};
    }

    LuaContext::ReloadScope::ReloadScope(LuaContext&                            context,
                                         std::span<const std::filesystem::path> changed,
                                         bool                                   replacing) :
        m_Context(context),
        m_PreviousModules(context.m_Modules),
        m_PreviousRoots(context.m_RootModules),
        m_Replacing(replacing)
    {
        if (context.m_Reloading || !context.m_LoadingModules.empty() || !context.m_CurrentRoot.empty())
        {
            throw std::logic_error("Lua module replacement must begin between callbacks");
        }
        std::set<std::string> invalidated;
        for (const auto& [name, module] : context.m_Modules)
        {
            if (!module.source.empty() && std::ranges::find(changed, module.source) != changed.end())
            {
                invalidated.insert(name);
            }
        }
        LuaStackScope stack(context.m_State);
        luaL_getsubtable(context.m_State, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
        const auto loaded = lua_gettop(context.m_State);
        lua_newtable(context.m_State);
        copyTable(context.m_State, loaded, -1);
        m_LoadedSnapshot      = luaL_ref(context.m_State, LUA_REGISTRYINDEX);
        context.m_Reloading   = true;
        context.m_ReloadScope = this;
        context.m_ReloadRoots.clear();
        try
        {
            invalidate(invalidated);
        }
        catch (...)
        {
            context.m_Reloading   = false;
            context.m_ReloadScope = nullptr;
            luaL_unref(context.m_State, LUA_REGISTRYINDEX, m_LoadedSnapshot);
            throw;
        }
    }

    void LuaContext::ReloadScope::refresh(std::string_view name)
    {
        std::set<std::string> changed;
        for (const auto& item : moduleClosure(m_Context.m_Modules, {std::string(name)}))
        {
            if (m_PendingInvalidations.contains(item))
            {
                continue;
            }
            const auto found = m_Context.m_Modules.find(item);
            if (found == m_Context.m_Modules.end() || found->second.source.empty())
            {
                continue;
            }
            std::error_code error;
            const auto      modified = std::filesystem::last_write_time(found->second.source, error);
            if (error || modified != found->second.modified ||
                std::filesystem::file_size(found->second.source, error) != found->second.size || error)
            {
                changed.insert(item);
            }
        }
        if (changed.empty())
        {
            return;
        }
        if (!m_Replacing)
        {
            for (const auto& [root, modules] : m_PreviousRoots)
            {
                if (!m_Context.m_RootUsers.contains(root))
                {
                    continue;
                }
                const auto used = moduleClosure(m_PreviousModules, modules);
                if (std::ranges::any_of(changed,
                                        [&](const std::string& module)
                                        {
                                            return used.contains(module);
                                        }))
                {
                    throw std::runtime_error("Reload dependent Lua scripts before attaching a changed module: " +
                                             std::string(name));
                }
            }
        }
        invalidate(changed);
    }

    void LuaContext::ReloadScope::invalidate(const std::set<std::string>& changed)
    {
        auto invalidated = changed;
        bool added       = true;
        while (added)
        {
            added = false;
            for (const auto& [name, module] : m_Context.m_Modules)
            {
                if (!invalidated.contains(name) && std::ranges::any_of(module.children,
                                                                       [&](const std::string& child)
                                                                       {
                                                                           return invalidated.contains(child);
                                                                       }))
                {
                    invalidated.insert(name);
                    added = true;
                }
            }
        }
        LuaStackScope stack(m_Context.m_State);
        m_PendingInvalidations.insert(invalidated.begin(), invalidated.end());
        luaL_getsubtable(m_Context.m_State, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
        const auto loaded = lua_gettop(m_Context.m_State);
        for (const auto& name : invalidated)
        {
            lua_pushlstring(m_Context.m_State, name.data(), name.size());
            lua_pushnil(m_Context.m_State);
            lua_rawset(m_Context.m_State, loaded);
            m_Context.m_Modules.at(name).children.clear();
        }
    }

    LuaContext::ReloadScope::~ReloadScope()
    {
        if (!m_Committed)
        {
            LuaStackScope stack(m_Context.m_State);
            luaL_getsubtable(m_Context.m_State, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
            const auto loaded = lua_gettop(m_Context.m_State);
            clearTable(m_Context.m_State, loaded);
            lua_rawgeti(m_Context.m_State, LUA_REGISTRYINDEX, m_LoadedSnapshot);
            copyTable(m_Context.m_State, -1, loaded);
            m_Context.m_Modules     = std::move(m_PreviousModules);
            m_Context.m_RootModules = std::move(m_PreviousRoots);
        }
        m_Context.m_Reloading   = false;
        m_Context.m_ReloadScope = nullptr;
        m_Context.m_ReloadRoots.clear();
        luaL_unref(m_Context.m_State, LUA_REGISTRYINDEX, m_LoadedSnapshot);
    }

    void LuaContext::ReloadScope::commit()
    {
        m_Committed = true;
    }
} // namespace vultra
