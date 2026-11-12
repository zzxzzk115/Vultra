# Vultra Editor

The editor of the Vultra ecosystem, on the `dev-editor` branch. It is a small ImGui desktop program on the
same host library as the `vultra` command line, so a project loads, reloads and fails here exactly as it does
with `vultra run`.

| Window | Shows |
| --- | --- |
| Viewport N | The project's tone-mapped image through that viewport's own camera. |
| Scene | The entities of the project's `.vscene` as a tree, with the nodes of each model below its entity; a click selects one. |
| Inspector | The components of the selected entity: transform, light and camera can be changed, the others are shown. |
| Pass parameters | A slider for every parameter of the project's graph. |
| Resources | The render path, the resources a graph can read with format, semantic and provider, and the passes of the frame. |
| Console | Loads and reloads, the diagnostics of an edit that was not applied, and what the kernel logs, such as shader compilation and asset import. Script problems stay at the top until fixed. |

Project files are watched. An edit that does not validate is listed in the console with its diagnostic code,
for example `VG002`, and the viewports keep the last image that worked.

## Projects

`File > Open project...` asks for a project directory or a `.vproject` file and replaces the open project.
A project that cannot be opened is reported in the console and the open one stays. Started without
`--project`, the editor is empty until a project is opened.

`--debug` (the default), `--release` and `--releasedbg` choose the project's profile, as they do for
`vultra run`.

For every project the editor keeps `.vultra/editor.json` in the project: the open viewports and their cameras.

## Scene

Changes in the inspector go to the running scene at once. `File > Save scene` writes the scene as it is now
over the project's `.vscene` file, in the form the host writes it: comments and the layout of the text are not
kept. An entity that an animation moves shows its animated values, and saving writes those.

## Layout

The windows are docked into one dock space: viewports in the centre; the scene, the inspector and, as tabs, the pass
parameters and the resources down the right; the console along the bottom. Every window can be dragged to another place, tabbed with another one or
pulled out of the main window. `View > Reset layout` brings the default back, and the `View` menu shows and
hides each window.

The docking belongs to the user, not to a project: it is kept in `~/.vultra/editor.ini`, or in the file
`--layout` names.

## Viewports

`View > New viewport` adds a viewport; it joins the first one as a tab and can be docked anywhere. Each
viewport has its own orbit camera, and the project is recorded once per open viewport in every frame: the
recordings are the views of the frame, numbered from 0, as the two eyes are in `vultra run --xr`. Work a
package does once per frame, such as building a panel's GUI, happens for view 0.

All viewports show images of one size, that of the viewport the mouse was over last; another viewport fits
that image into its window without distortion. With two viewports the default layout puts them side by side.

### Input

The viewport the mouse is over gives the project its input: the keys and mouse buttons of the window, with
the mouse position in that viewport's image. That viewport is view 0 of the frame, so a package that turns
the mouse into a ray, as the panel package does, uses the camera the user is looking through.

Holding Alt gives the mouse to the camera instead: left drag orbits, middle or right drag pans, the wheel
dollies. While Alt is held, and while the mouse is over no viewport, the project sees every key and button
released.

## Native packages

The stock editor carries the scripting package, like the stock command line. A project that lists other
native packages is opened with

```sh
vultra edit my-project
```

which builds an editor with those packages into `<project>/.vultra/build/<name>-editor`, the way `vultra run`
builds the project's own executable. When the source of one of those packages changes, that editor saves its
viewports and the values of the graph's parameters, ends, is built again and started again where it was. A
build that fails waits for the next change.

Opened in an editor that lacks one of its packages, a project still loads; the console names the missing
packages and the project's script fails where it uses them.

## Build

The editor is built against sibling checkouts of
[Vultra-CLI](https://github.com/zzxzzk115/Vultra-CLI) (the host library),
[libvultra](https://github.com/zzxzzk115/libvultra) (the kernel, branch `dev-slim`) and
[Vultra-Packages](https://github.com/zzxzzk115/Vultra-Packages) (the scripting package):

```
GitHubProjects/
  Vultra/           this repository
  Vultra-CLI/
  Vultra-Packages/
  libvultra/
```

Install xmake and a C++23 compiler, then:

```sh
xmake f -m release -y
xmake build vultra-editor
```

`--vultra_cli_dir=<path>` selects another Vultra-CLI checkout. `main()` is generated from the manifests of
the packages that are compiled in; it registers them and calls `vultra::editor::commandLine`.

## Run

```sh
vultra-editor [--project DIR] [--debug | --release | --releasedbg] [--viewports N] [--layout FILE]
              [--frames N] [--capture file.png]
vultra edit [project] [--debug | --release | --releasedbg] [--layout FILE] [--verbose]
```

`--viewports` opens that many viewports instead of those the project had. `--frames` closes the editor after
N frames; `--capture` saves the window of the last frame. A project named with `--project` that cannot be
opened ends the command with exit code `1`.

`vultra edit` uses the stock editor for a project it can open: the program the environment variable
`VULTRA_EDITOR` names, `vultra-editor` beside the `vultra` executable, or `vultra-editor` on `PATH`. To build
an editor with packages it needs this checkout: the one `VULTRA_EDITOR_SDK` names, or the `Vultra` directory
beside the Vultra-CLI checkout.

## Tests

```sh
vultra lua tests/run.lua --window
```

The script runs the editor on projects it creates under `build/.tmp/editor-tests`, with layout files of its
own. Every case opens a window, so without `--window` all of them are skipped. The last case builds an
editor with a copy of the panel package and edits that copy, which takes several minutes the first time.

## Limits

- Verified on Windows 11 with Vulkan only, by the test script and by captures of the window. Dragging and
  detaching windows, the camera, and keys or clicks reaching a project were not operated by a person.
- Each viewport costs one more recording and one more wait for the GPU per frame.
- A viewport shows the display image only, not another output of the graph.
- A project is opened by typing its path; there is no file dialog.
- Entities cannot be added, removed, renamed or dragged in the viewport, and components cannot be added.
- There is no node graph editor, asset browser or undo.

## Earlier work

The engine core that this repository held before (an EnTT world, scripting hosts, generated interfaces, the
earlier editor and hub) is on the `dev-VRI` branch. It is the source for later packages, not part of this
branch.
