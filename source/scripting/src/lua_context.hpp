#pragma once

#include <vultra/api/native_plugin.h>
#include <vultra/api/scene_bridge.hpp>
#include <vultra/core/base/property.hpp>
#include <vultra/core/base/stable_id.hpp>

#include <nlohmann/json_fwd.hpp>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <filesystem>
#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace vultra
{
    struct LuaStackScope
    {
        lua_State* state;
        int        top;

        explicit LuaStackScope(lua_State* state) :
            state(state),
            top(lua_gettop(state))
        {
        }

        ~LuaStackScope()
        {
            lua_settop(state, top);
        }

        LuaStackScope(const LuaStackScope&)            = delete;
        LuaStackScope& operator=(const LuaStackScope&) = delete;
    };

    // Owned by ScriptHost before its instances; API closures and cached object views borrow this context.
    class LuaContext
    {
        struct Module
        {
            std::filesystem::path           source;
            std::set<std::string>           children;
            std::filesystem::file_time_type modified {};
            uintmax_t                       size = 0;
        };

        using Modules = std::map<std::string, Module>;
        using Roots   = std::map<std::filesystem::path, std::set<std::string>>;

    public:
        class ReloadScope
        {
        public:
            ReloadScope(LuaContext& context, std::span<const std::filesystem::path> changed, bool replacing = true);
            ~ReloadScope();
            ReloadScope(const ReloadScope&)            = delete;
            ReloadScope& operator=(const ReloadScope&) = delete;
            void         commit();

        private:
            friend class LuaContext;
            void                  refresh(std::string_view module);
            void                  invalidate(const std::set<std::string>& modules);
            LuaContext&           m_Context;
            Modules               m_PreviousModules;
            Roots                 m_PreviousRoots;
            int                   m_LoadedSnapshot = LUA_NOREF;
            bool                  m_Committed      = false;
            bool                  m_Replacing;
            std::set<std::string> m_PendingInvalidations;
        };

        LuaContext(SceneTree& scene, const ProjectManifest* project);
        ~LuaContext();
        LuaContext(const LuaContext&)            = delete;
        LuaContext& operator=(const LuaContext&) = delete;

        lua_State*               state() const;
        SceneTree&               scene() const;
        const ProjectManifest*   project() const;
        void                     setModuleRoot(const std::filesystem::path& root);
        int                      instantiateScript(const std::filesystem::path& path);
        void                     releaseScript(int reference, const std::filesystem::path& path);
        void                     attachScript(int reference, ObjectId node);
        void                     pushEditorGui();
        void                     pushInputEvent();
        std::string              declarationSource(lua_State* state, int index) const;
        std::vector<std::string> dependencies(const std::filesystem::path& root) const;
        nlohmann::json           readValue(const PropertySchema& property, lua_State* state, int index) const;
        void pushValue(const PropertySchema& property, lua_State* state, const nlohmann::json& value) const;

    private:
        friend class LuaCallbackScope;
        struct Api;
        void       initializeApi();
        void       initializeModules();
        void       rememberDeclaration(lua_State* state, int index, const std::filesystem::path& source);
        int        require(lua_State* state);
        static int requireModule(lua_State* state);
        static std::set<std::string> moduleClosure(const Modules& modules, const std::set<std::string>& roots);
        void                         bind(const char* name, lua_CFunction function);
        static int                   instanceIndex(lua_State* state);
        nlohmann::json               readReference(const PropertySchema& property, lua_State* state, int index) const;
        void                         pushReference(const PropertySchema& property,
                                                   lua_State*            state,
                                                   const nlohmann::json& value,
                                                   bool                  arrayElement) const;

        SceneTree&                              m_Scene;
        const ProjectManifest*                  m_Project;
        lua_State*                              m_State;
        int                                     m_AttachRef  = LUA_NOREF;
        int                                     m_GuiRef     = LUA_NOREF;
        int                                     m_WrapRef    = LUA_NOREF;
        int                                     m_RequireRef = LUA_NOREF;
        int                                     m_OriginsRef = LUA_NOREF;
        Modules                                 m_Modules;
        Roots                                   m_RootModules;
        std::map<std::filesystem::path, size_t> m_RootUsers;
        std::filesystem::path                   m_CurrentRoot;
        std::filesystem::path                   m_ModuleRoot;
        std::vector<std::string>                m_LoadingModules;
        std::set<std::filesystem::path>         m_ReloadRoots;
        bool                                    m_Reloading   = false;
        ReloadScope*                            m_ReloadScope = nullptr;
        VultraSceneFrame                        m_SceneFrame {};
        VultraUiFrame                           m_UiFrame {};
    };

    // Nested property calls temporarily replace the active borrow and restore the outer lifecycle/UI phase.
    class LuaCallbackScope
    {
    public:
        LuaCallbackScope(LuaContext& context, VultraSceneFrame scene = {}, VultraUiFrame ui = {});
        ~LuaCallbackScope();
        LuaCallbackScope(const LuaCallbackScope&)            = delete;
        LuaCallbackScope& operator=(const LuaCallbackScope&) = delete;

    private:
        LuaContext&      m_Context;
        VultraSceneFrame m_PreviousScene;
        VultraUiFrame    m_PreviousUi;
    };

    void           pushLuaProperty(lua_State* state, const nlohmann::json& value, bool arrayElement = false);
    nlohmann::json readLuaProperty(lua_State* state, int index, int depth = 0);
} // namespace vultra
