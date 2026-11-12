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

-- `vultra edit` builds an editor with a project's native packages under the project's own name.
option("vultra_editor_executable")
    set_default("vultra-editor")
    set_showmenu(true)
    set_description("File name of the editor executable, without extension")
option_end()

local host = path.absolute(get_config("vultra_cli_dir") or "../Vultra-CLI", os.scriptdir())
includes(host)
local library = path.absolute(get_config("libvultra_dir") or "../libvultra", host)

-- Native packages are compiled into the editor exactly as into the command line: the scripting package the
-- stock executables carry, and those the options vultra_packages and vultra_requires add.
local package_targets, package_dirs = vultraPackages()

target("vultra-editor")
    set_kind("binary")
    set_basename(get_config("vultra_editor_executable") or "vultra-editor")
    if (get_config("vultra_executable_dir") or "") ~= "" then
        set_targetdir(get_config("vultra_executable_dir"))
    end
    add_deps("vultra-host")
    if #package_targets > 0 then
        add_deps(table.unpack(package_targets))
    end
    add_deps("vultra-builtin-pack", {inherit = false})
    add_includedirs("source/editor")
    add_files("source/editor/*.cpp")
    -- main() is generated: it registers every package and calls the editor's command line.
    if #package_dirs > 0 then
        set_values("vultra.package_dirs", table.unpack(package_dirs))
    end
    set_values("vultra.package_entry", "vultra::editor::commandLine")
    set_values("vultra.package_entry_header", '"editor.hpp"')
    add_rules("vultra.package_main")
    -- The cooked builtin shaders are embedded the same way as in the command line.
    if is_plat("windows") then
        add_files(path.join(library, "builtin", "builtin_pack.rc"))
    else
        add_files(path.join(library, "builtin", "builtin_pack.S"))
    end
    set_rundir("$(projectdir)")
target_end()
