-- Tests of the editor, run by the `vultra` executable as a tool script:
--
--     vultra lua tests/run.lua --window [--editor path/to/vultra-editor] [--verbose]
--
-- Every case opens a window, so without --window all of them are skipped. The editor under test is the one
-- --editor names, or the release build of this checkout. The test runner comes from the sibling
-- Vultra-Packages checkout. Projects, layouts and captures are written to build/.tmp/editor-tests; the
-- user's own layout is never touched.
local here = vultra.fs.absolute(arg[0]):gsub("/[^/]*$", "")
local repository = here:gsub("/[^/]*$", "")
local support = dofile(repository .. "/../Vultra-Packages/tests/support.lua")
local suffix = support.vultra:match("%.exe$") or ""

local editor = repository .. "/build/windows/x64/release/vultra-editor" .. suffix
for index = 1, #arg do
    if arg[index] == "--editor" and arg[index + 1] then
        editor = vultra.fs.absolute(arg[index + 1])
    end
end

local root = repository .. "/build/.tmp/editor-tests/" .. os.time() .. "-" ..
                 math.floor(vultra.process.clock() * 1000) % 100000
vultra.fs.mkdir(root)

-- A research project of its own for one case.
local function project(name)
    local directory = root .. "/" .. name
    local code, output = support.run(support.vultra, {"new", "research", directory})
    support.expect(code, output, 0)
    return directory
end

-- Waits until a running program has printed `text`, and fails the case when it has not after `seconds`.
local function printed(process, text, seconds)
    local deadline = vultra.process.clock() + seconds
    while true do
        local code, output = support.wait(process, 0.5)
        if output:find(text, 1, true) then
            return output
        end
        if code or vultra.process.clock() >= deadline then
            process:stop()
            support.fail("the editor did not print '" .. text .. "'\n" .. output)
        end
    end
end

local function count(text, pattern)
    local found = 0
    for _ in text:gmatch(pattern) do
        found = found + 1
    end
    return found
end

support.test("the editor opens a project and keeps its viewports and the layout", function()
    if not vultra.fs.exists(editor) then
        support.fail("no editor at " .. editor .. "; build vultra-editor or pass --editor")
    end
    local directory = project("open")
    local layout = root .. "/open-layout.ini"
    local code, output = support.run(editor, {"--project", directory, "--viewports", "2", "--frames", "60",
                                              "--layout", layout, "--capture", root .. "/open.png"})
    support.expect(code, output, 0, "loaded open (debug)")
    if output:find("VUID", 1, true) then
        support.fail("the GPU validation reported a problem\n" .. output)
    end
    if not vultra.fs.exists(root .. "/open.png") or not vultra.fs.exists(layout) then
        support.fail("the run left no capture or no layout file")
    end
    local state = support.read(directory .. "/.vultra/editor.json")
    if count(state, '"number"') ~= 2 then
        support.fail("the project's editor state does not hold two viewports\n" .. state)
    end
    -- Without --viewports the project opens with the viewports it had, and the saved layout is read back.
    local saved = support.read(layout)
    code, output = support.run(editor, {"--project", directory, "--release", "--frames", "60", "--layout", layout,
                                        "--capture", root .. "/open-again.png"})
    support.expect(code, output, 0, "loaded open (release)")
    if count(support.read(directory .. "/.vultra/editor.json"), '"number"') ~= 2 then
        support.fail("the second start did not restore the two viewports")
    end
    if support.read(layout) ~= saved then
        support.fail("reading the saved layout back changed it")
    end
end, {window = true})

support.test("a project that cannot be opened ends the command, and no project is an empty editor", function()
    local code, output = support.run(editor, {"--project", root .. "/missing", "--frames", "5", "--layout",
                                              root .. "/missing-layout.ini"})
    support.expect(code, output, 1, "could not open")
    code, output = support.run(editor, {"--frames", "30", "--layout", root .. "/empty-layout.ini"})
    support.expect(code, output, 0)
    code, output = support.run(editor, {"--debug", "--release"})
    support.expect(code, output, 2, "Choose one of")
end, {window = true})

support.test("a material is inspected without being edited", function()
    -- The inspector starts on the scene's first entity, here the model with a material component.
    local directory = project("material")
    local scene = directory .. "/main.vscene"
    support.edit(scene, '{ "model": { "file": "assets/sphere.gltf" } }',
                 '{ "model": { "file": "assets/sphere.gltf" }, "material": { "type": "vultra.openpbr", ' ..
                     '"parameters": { "base_color": [0.9, 0.3, 0.2] } } }')
    local before = support.read(scene)
    local code, output = support.run(editor, {"--project", directory, "--frames", "60", "--layout",
                                              root .. "/material-layout.ini", "--capture", root .. "/material.png"})
    support.expect(code, output, 0, "loaded material (debug)")
    if output:find("VUID", 1, true) then
        support.fail("the GPU validation reported a problem\n" .. output)
    end
    if support.read(scene) ~= before then
        support.fail("showing the material changed the scene file")
    end
end, {window = true})

support.test("a graph that does not validate is reported and the fixed graph is loaded again", function()
    local directory = project("reload")
    local started = vultra.process.start(editor, {"--project", directory, "--frames", "1000000", "--layout",
                                                  root .. "/reload-layout.ini"})
    printed(started, "loaded reload", 60)
    support.sleep(2)
    support.edit(directory .. "/main.vgraph", "scene.hdr", "scene.hrd")
    local output = printed(started, "VG002", 30)
    if not output:find("reload failed, keeping the previous frame", 1, true) then
        started:stop()
        support.fail("the rejected edit was not reported as one\n" .. output)
    end
    support.edit(directory .. "/main.vgraph", "scene.hrd", "scene.hdr")
    printed(started, "reloaded reload", 30)
    started:stop()
end, {window = true})

-- Before the edit: sets a graph parameter. After it: checks that the parameter survived the restart.
local restarting = [[
local frames = 0
function vultra.update(dt)
    frames = frames + 1
    if vultra.panel.revision == 2 then
        if frames == 5 then
            vultra.quit(math.abs(vultra.graph.get("gain.gain") - 3.5) < 1e-6 and 42 or 43)
        end
    elseif frames == 5 then
        vultra.graph.set("gain.gain", 3.5)
    end
end
]]

support.test("vultra edit builds an editor with a native package and rebuilds it when its source changes", function()
    local package_directory = repository .. "/../Vultra-Packages/panel"
    local copy = root .. "/panel-copy"
    for _, part in ipairs({"include", "source", "shaders", "models", "vultra.package.json"}) do
        vultra.fs.copy(package_directory .. "/" .. part, copy .. "/" .. part)
    end
    local directory = project("restart")
    local name = '"name": "restart",'
    support.edit(directory .. "/restart.vproject", name,
                 name .. '\n  "entry": "main.lua",\n  "packages": { "vultra.panel": { "path": "../panel-copy" } },')
    support.write(directory .. "/main.lua", restarting)
    local started = vultra.process.start(support.vultra, {"edit", directory, "--layout",
                                                          root .. "/restart-layout.ini"})
    -- The window opens once the project's editor is built.
    printed(started, "loaded restart", 900)
    support.sleep(5)
    local marker = 'lua_setfield(state, -2, "pointer");'
    support.edit(copy .. "/source/panel.cpp", marker,
                 marker .. '\n            lua_pushinteger(state, 2);\n            lua_setfield(state, -2, "revision");')
    local code, output = support.wait(started, 900)
    if code == nil then
        started:stop()
        support.fail("the edited package was not rebuilt and the editor not restarted:\n" .. output)
    end
    if code == 43 then
        support.fail("the graph parameter was not restored\n" .. output)
    end
    support.expect(code, output, 42, "native sources changed")
    if output:find("is not part of this editor", 1, true) then
        support.fail("the editor was built without the project's package\n" .. output)
    end
    if not vultra.fs.exists(directory .. "/.vultra/build/restart-editor" .. suffix) then
        support.fail("the project's editor was not written into the project")
    end
end, {window = true})

return support.finish()
