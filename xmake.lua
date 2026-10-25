set_project("Vultra")
set_languages("cxx23")
add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", {outputdir = "."})

option("libvultra_dir")
    set_default("external/libvultra")
    set_showmenu(true)
    set_description("Pinned library checkout, or an explicit local source override")
option_end()
local function selectFeatures(requested)
    local requirements = {
        world = {},
        rendering = {"world"},
        asset_import = {"world"},
        scene_prepare = {"world"},
        export = {"asset_import", "shader"},
        shader = {"world"},
        desktop = {"world"},
        editor_gui = {"world"},
        vgui = {"world"},
        script_native = {"world"},
        script_lua = {"world"},
        script_csharp = {"world"}
    }
    local selected = {}
    local failure
    local function enable(name)
        if not requirements[name] then
            failure = "Unknown Vultra capability: " .. name
            return
        end
        if selected[name] then
            return
        end
        selected[name] = true
        for _, dependency in ipairs(requirements[name]) do
            enable(dependency)
        end
    end
    local count = 0
    for name in requested:gmatch("[^,]+") do
        name = name:trim()
        count = count + 1
        if name == "all" then
            for capability in pairs(requirements) do
                enable(capability)
            end
        else
            enable(name)
        end
    end
    if count == 0 then
        failure = "vultra_features must name at least one capability"
    end
    return selected, failure
end
local function libraryFeatures(selected)
    local required = {"asset_values", "material_values", "asset_source", "input_actions", "random", "render_values"}
    local dependencies = {
        rendering = {"rendering", "graph_json"},
        asset_import = {"asset_import"},
        scene_prepare = {"scene_prepare"},
        export = {"game_shader"},
        shader = {"game_shader"},
        desktop = {"desktop", "camera"},
        editor_gui = {"editor_gui"},
        vgui = {"vgui"}
    }
    for name in pairs(selected) do
        table.join2(required, dependencies[name] or {})
    end
    required = table.unique(required)
    table.sort(required)
    return table.concat(required, ",")
end
option("vultra_features")
    set_default("all")
    set_showmenu(true)
    set_description("Engine capabilities: world, rendering, asset_import, scene_prepare, export, shader, desktop, " ..
                    "editor_gui, vgui, script_native, script_lua, script_csharp")
    -- Derived requirements must follow CLI option checking, not the initial menu pass.
    after_check(function ()
        import("core.project.config")
        assert(not config.readonly("libvultra_features"),
               "Select vultra_features; the engine derives its library requirements")
        local selected, failure = selectFeatures(config.get("vultra_features") or "all")
        assert(not failure, failure)
        config.set("libvultra_features", libraryFeatures(selected))
    end)
option_end()

function vultraLibraryRoot()
    return path.absolute(get_config("libvultra_dir") or "external/libvultra", os.projectdir())
end

function vultraEngineFeatures()
    return selectFeatures(get_config("vultra_features") or "all")
end
local features = vultraEngineFeatures()
set_config("libvultra_build_examples", false)
set_config("libvultra_build_tests", false)
includes(vultraLibraryRoot())
add_requires("entt v3.15.0", {system = false})
if features.script_lua then
    add_requires("lua v5.4.8", {configs = {shared = false}, system = false})
end
includes("source")

includes("tests")

task("codegen")
    set_menu {
        usage = "xmake codegen [options]",
        options = {{"c", "check", "k", nil, "Check generated files without modifying them"}}
    }
    on_run(function ()
        import("core.base.option")
        import("core.project.config")
        config.load()
        local selected = selectFeatures(config.get("vultra_features") or "all")
        assert(selected.rendering and selected.editor_gui,
               "Generate the complete API with rendering and editor_gui selected, then refresh compile_commands")
        local library = path.absolute(config.get("libvultra_dir") or "external/libvultra", os.projectdir())
        local args = {path.join(library, "scripts/codegen.py"), "--manifest",
                      path.join(os.projectdir(), "source/api/codegen.json"), "--database",
                      path.join(os.projectdir(), "compile_commands.json")}
        if option.get("check") then
            table.insert(args, "--check")
        end
        os.execv("python3", args)
    end)
task_end()
