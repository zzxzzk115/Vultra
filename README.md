# Vultra Editor

The editor of the Vultra ecosystem, on the `dev-editor` branch. It is a small ImGui desktop program on the
same host library as the `vultra` command line, so a project loads, reloads and fails here exactly as it does
with `vultra run`.

| Window | Shows |
| --- | --- |
| Viewport | The project's tone-mapped image at the window's size. Left drag orbits, middle or right drag pans, the wheel dollies. |
| Pass parameters | A slider for every parameter of the project's graph. |
| Resources | The render path, the resources a graph can read with format, semantic and provider, and the passes of the frame. |
| Console | Loads and reloads, and the diagnostics of an edit that was not applied. Script problems stay at the top until fixed. |

Project files are watched. An edit that does not validate is listed in the console with its diagnostic code,
for example `VG002`, and the viewport keeps the last image that worked.

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
vultra-editor [project] [--frames N] [--capture file.png]
vultra edit [project]
```

`project` is a project directory or a `.vproject` file and defaults to the current directory. `--frames`
closes the editor after N frames; `--capture` saves the window of the last frame. `vultra edit` finds the
editor through the environment variable `VULTRA_EDITOR`, beside the `vultra` executable, or on `PATH`.

The window layout is stored in the project, under `.vultra/editor.ini`.

## Limits

- Verified on Windows 11 with Vulkan only. The docking, resizing and camera were not operated by a person.
- The project gets no keyboard or mouse input from the viewport.
- The editor carries the scripting package only. For a project with other native packages the console names
  the missing ones; run such a project with `vultra run`.
- There is no node graph editor, scene hierarchy, asset browser or undo.

## Earlier work

The engine core that this repository held before (an EnTT world, scripting hosts, generated interfaces, the
earlier editor and hub) is on the `dev-VRI` branch. It is the source for later packages, not part of this
branch.
