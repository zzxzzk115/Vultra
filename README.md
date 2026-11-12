# Vultra Editor

The editor of the Vultra ecosystem, on the `dev-editor` branch. It is a small ImGui desktop program on the
same host library as the `vultra` command line, so a project loads, reloads and fails here exactly as it does
with `vultra run`.

| Window | Shows |
| --- | --- |
| Viewport N | The project's tone-mapped image through that viewport's own camera. Left drag orbits, middle or right drag pans, the wheel dollies. |
| Pass parameters | A slider for every parameter of the project's graph. |
| Resources | The render path, the resources a graph can read with format, semantic and provider, and the passes of the frame. |
| Console | Loads and reloads, and the diagnostics of an edit that was not applied. Script problems stay at the top until fixed. |

Project files are watched. An edit that does not validate is listed in the console with its diagnostic code,
for example `VG002`, and the viewports keep the last image that worked.

## Layout

The windows are docked into one dock space: viewports in the centre, pass parameters above resources on the
right, the console along the bottom. Every window can be dragged to another place, tabbed with another one or
pulled out of the main window. The docking is stored in the project, under `.vultra/editor.ini`, and
`View > Reset layout` brings the default back. The `View` menu also shows and hides each window.

## Viewports

`View > New viewport` adds a viewport; it joins the first one as a tab and can be docked anywhere. Each
viewport has its own orbit camera, and the project is recorded once per open viewport in every frame: the
recordings are the views of the frame, numbered from 0, as the two eyes are in `vultra run --xr`. Work a
package does once per frame, such as building a panel's GUI, happens for view 0.

All viewports show images of one size, that of the viewport the mouse was over last; another viewport fits
that image into its window without distortion. With two viewports at the start the default layout puts them
side by side.

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

`--vultra_cli_dir=<path>` selects another Vultra-CLI checkout.

## Run

```sh
vultra-editor [--project DIR] [--viewports N] [--frames N] [--capture file.png]
vultra edit [project]
```

The project is a project directory or a `.vproject` file and defaults to the current directory. `--viewports`
opens that many viewports at the start. `--frames` closes the editor after N frames; `--capture` saves the
window of the last frame. `vultra edit` finds the
editor through the environment variable `VULTRA_EDITOR`, beside the `vultra` executable, or on `PATH`.

## Limits

- Verified on Windows 11 with Vulkan only, from captures of the default layout with one and two viewports and
  of a saved layout that was read back. Dragging windows, detaching them and the camera were not operated by a
  person.
- Each viewport costs one more recording and one more wait for the GPU per frame. Which viewports are open is
  not stored; the editor starts with the number `--viewports` names.
- A viewport shows the display image only, not another output of the graph.
- The project gets no keyboard or mouse input from the viewport.
- The editor carries the scripting package only. For a project with other native packages the console names
  the missing ones; run such a project with `vultra run`.
- There is no node graph editor, scene hierarchy, asset browser or undo.

## Earlier work

The engine core that this repository held before (an EnTT world, scripting hosts, generated interfaces, the
earlier editor and hub) is on the `dev-VRI` branch. It is the source for later packages, not part of this
branch.
