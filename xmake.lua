set_project("Vultra")
set_version("0.1.0")
set_languages("cxx23")
add_rules("mode.debug", "mode.release")

if is_plat("windows") then
    -- Every target and package must share one MSVC runtime; the static one keeps the executable standalone.
    local msvc_runtime = is_mode("debug") and "MTd" or "MT"
    set_runtimes(msvc_runtime)
    add_requireconfs("**", {configs = {runtimes = msvc_runtime}})
    add_cxxflags("/Zc:__cplusplus", "/bigobj", "/EHsc", {tools = {"msvc", "cl"}})
end

-- The editor is a front end of the host library the command line is built on. That library, the kernel it
-- includes and the scripting package come from sibling checkouts of Vultra-CLI, libvultra and Vultra-Packages.
option("vultra_cli_dir")
    set_default("../Vultra-CLI")
    set_showmenu(true)
    set_description("Vultra-CLI checkout that provides the host library")
option_end()

local host = path.absolute(get_config("vultra_cli_dir") or "../Vultra-CLI", os.scriptdir())
includes(host)
local library = path.absolute(get_config("libvultra_dir") or "../libvultra", host)

target("vultra-editor")
    set_kind("binary")
    -- package-1 is the scripting package the stock command line carries.
    add_deps("vultra-host", "package-1")
    add_deps("vultra-builtin-pack", {inherit = false})
    add_files("source/editor/*.cpp")
    -- The cooked builtin shaders are embedded the same way as in the command line.
    if is_plat("windows") then
        add_files(path.join(library, "builtin", "builtin_pack.rc"))
    else
        add_files(path.join(library, "builtin", "builtin_pack.S"))
    end
    set_rundir("$(projectdir)")
target_end()
