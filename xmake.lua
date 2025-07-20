-- set project name
set_project("Vultra")

-- set project version
set_version("0.1.0")

-- set language version: C++ 23
set_languages("cxx23")

-- global options
option("examples") -- build examples?
    set_default(true)
option_end()

option("tests") -- build tests?
    set_default(true)
option_end()

-- if build on windows
if is_plat("windows") then
    add_cxxflags("/Zc:__cplusplus", {tools = {"msvc", "cl"}}) -- fix __cplusplus == 199711L error
    add_cxxflags("/bigobj") -- avoid big obj
    add_cxxflags("-D_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING")
    add_cxxflags("/EHsc")
    if is_mode("debug") then
        set_runtimes("MDd")
        add_links("ucrtd")
    else
        set_runtimes("MD")
    end
else
    add_cxxflags("-fexceptions")
end

-- global rules
rule("copy_assets")
    on_load(function(target)
        -- https://xmake.io/#/zh-cn/manual/builtin_modules?id=oscp
        os.cp("$(projectdir)/resources/*", target:targetdir() .. "/resources", {rootdir = "resources"})
    end)
rule_end()

add_rules("mode.debug", "mode.release")
add_rules("plugin.vsxmake.autoupdate")
add_rules("plugin.compile_commands.autoupdate", {outputdir = ".vscode"})

-- add the official xmake repository
add_repositories("xmake-repo https://github.com/xmake-io/xmake-repo.git dev")

-- include source
includes("src")

-- include tests
if has_config("tests") then
    includes("tests")
end

-- if build examples, then include examples
if has_config("examples") then
    includes("examples")
end