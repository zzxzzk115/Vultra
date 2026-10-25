local features = vultraEngineFeatures()
local diagnostics = path.join(vultraLibraryRoot(), "scripts/test_diagnostics.lua")
if os.isfile(diagnostics) then
    includes(diagnostics)
end

target("test-world-random")
    set_kind("binary")
    set_default(false)
    add_deps("vultra-engine")
    add_packages("nlohmann_json")
    add_files("scene_random.cpp")
    set_rundir("$(projectdir)")
    add_rules("vultra.test.diagnostics")
    add_tests("default")
target_end()

target("test-resources")
    set_kind("binary")
    set_default(false)
    add_deps("vultra-engine")
    add_packages("nlohmann_json")
    add_files("resources.cpp")
    set_rundir("$(projectdir)")
    add_rules("vultra.test.diagnostics")
    add_tests("default")
target_end()

target("test-components")
    set_kind("binary")
    set_default(false)
    add_deps("vultra-engine")
    add_packages("nlohmann_json")
    add_files("components.cpp")
    set_rundir("$(projectdir)")
    add_rules("vultra.test.diagnostics")
    add_tests("default")
target_end()

if features.asset_import then
    target("test-scene")
        set_kind("binary")
        set_default(false)
        add_deps("vultra-engine")
        add_packages("nlohmann_json")
        add_files("scene.cpp")
        set_rundir("$(projectdir)")
        add_rules("vultra.test.diagnostics")
        add_tests("default")
    target_end()
end

target("test-scene-execution")
    set_kind("binary")
    set_default(false)
    add_deps("vultra-engine")
    add_packages("nlohmann_json")
    add_files("scene_execution.cpp")
    set_rundir("$(projectdir)")
    add_rules("vultra.test.diagnostics")
    add_tests("default")
target_end()

target("test-ecs")
    set_kind("binary")
    set_default(false)
    add_deps("vultra-engine")
    add_packages("nlohmann_json")
    add_files("ecs.cpp")
    set_rundir("$(projectdir)")
    add_rules("vultra.test.diagnostics")
    add_tests("default")
target_end()

target("test-scene-input")
    set_kind("binary")
    set_default(false)
    add_deps("vultra-engine")
    add_packages("nlohmann_json")
    add_files("scene_input.cpp")
    set_rundir("$(projectdir)")
    add_rules("vultra.test.diagnostics")
    add_tests("default")
target_end()

if features.rendering then
    target("test-properties")
        set_kind("binary")
        set_default(false)
        add_deps("vultra-engine")
        add_packages("nlohmann_json")
        add_files("properties.cpp")
        set_rundir("$(projectdir)")
        add_rules("vultra.test.diagnostics")
        add_tests("default")
    target_end()
end

target("test-abi-contract")
    set_kind("binary")
    set_default(false)
    add_deps("vultra-engine", "vultra-native-sdk")
    add_files("abi_contract.cpp")
    if features.editor_gui then
        add_defines("VULTRA_TEST_GUI")
    end
    set_rundir("$(projectdir)")
    add_rules("vultra.test.diagnostics")
    add_tests("default")
    after_build(function (target)
        import("core.project.config")
        if config.get("vultra_features") ~= "world" then
            return
        end
        import("core.project.project")
        for name in pairs(project.required_packages()) do
            assert(not table.contains({"vri", "slang-static", "imgui", "rmlui", "lua", "libsdl3", "glfw",
                                       "vtask", "ispc", "directxtex", "meshoptimizer", "tinygltf", "tinyobjloader",
                                       "openfbx"}, name), "CPU world selected unrelated package: " .. name)
        end
        for _, file in ipairs(target:dep("vultra-engine"):dep("vultra"):sourcefiles()) do
            assert(not file:find("/drivers/", 1, true) and not file:find("/ui/", 1, true),
                   "CPU world compiled graphics/UI implementation: " .. file)
        end
    end)
target_end()

if features.rendering and features.shader then
    target("test-gpu-scene-sync")
        set_kind("binary")
        set_default(false)
        add_deps("vultra-engine")
        add_files("gpu_scene_sync.cpp")
        set_rundir(vultraLibraryRoot())
        add_rules("vultra.test.diagnostics")
        add_tests("default")
    target_end()
end
