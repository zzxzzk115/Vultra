local library = vultraLibraryRoot()
local features, featureError = vultraEngineFeatures()

target("vultra-engine")
    on_load(function ()
        assert(not featureError, featureError)
    end)
    set_kind("static")
    add_deps("vultra", {public = true})
    add_cxflags("-fPIC", {tools = {"gcc", "clang"}})
    for _, module in ipairs({"scene", "assets", "main", "api", "scripting"}) do
        add_includedirs(module .. "/include", {public = true})
    end
    add_files("scene/src/*.cpp", "api/src/*.cpp", "assets/src/project_manifest.cpp",
              "main/src/project_types.cpp")
    remove_files("api/src/render_settings.generated.cpp", "api/src/vultra_ui.generated.cpp")
    add_packages("entt", "nlohmann_json", {public = true})
    if features.editor_gui then
        add_defines("VULTRA_API_UI")
    else
        remove_files("api/src/scene_properties.generated.cpp")
    end
    if not features.rendering then
        remove_files("scene/src/scene_shader_materials.cpp", "scene/src/scene_gpu_sync.cpp")
    end
    if not features.asset_import then
        remove_files("scene/src/scene_import.cpp")
    end
    if features.export then
        add_files("assets/src/vpk_export.cpp")
    end
    if is_plat("linux") then
        add_syslinks("dl")
    end
target_end()

target("vultra-native-sdk")
    set_kind("headeronly")
    set_default(false)
    add_includedirs("api/include", "scripting/include", path.join(library, "source/core/include"),
                    path.join(library, "source/api/include"), {public = true})
    add_packages("nlohmann_json", {public = true})
target_end()

if features.script_native or features.script_lua or features.script_csharp then
    target("vultra-scripting")
        set_kind("static")
        set_default(false)
        add_deps("vultra-engine", {public = true})
        add_files("scripting/src/script_host.cpp", "scripting/src/script_properties.cpp")
        if features.script_native then
            add_files("scripting/src/native_script.cpp")
            add_defines("VULTRA_SCRIPT_NATIVE")
        end
        if features.script_lua then
            add_files("scripting/src/lua_*.cpp")
            add_packages("lua")
            add_defines("VULTRA_SCRIPT_LUA")
        end
        if features.script_csharp then
            add_files("scripting/src/dotnet_script.cpp")
            add_defines("VULTRA_SCRIPT_CSHARP")
        end
        if features.editor_gui then
            add_defines("VULTRA_SCRIPT_GUI")
        end
        if is_plat("linux", "macosx") then
            add_cxflags("-fPIC")
            add_syslinks("dl")
        end
    target_end()

end

if features.script_csharp then
    target("vultra-managed-host")
        set_kind("phony")
        set_default(false)
        set_policy("build.fence", true)
        on_build(function ()
            local output = path.join(os.projectdir(), "build/.tmp/scripting-managed")
            os.execv("dotnet", {"build", "source/scripting/managed/generators/Vultra.Scripting.Generators.csproj",
                               "-c", "Release", "--nologo"})
            os.execv("dotnet", {"build", "source/scripting/managed/Vultra.ManagedHost.csproj",
                               "-c", "Release", "-o", output, "--nologo"})
        end)
    target_end()
end
