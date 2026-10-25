# Build and verification

## Requirements

Use xmake, Git and a C++23 compiler. Windows builds need the MSVC C++ toolchain; Linux builds need GCC or Clang.
Graphics configurations additionally need a supported Vulkan driver and the window-system development packages.
The optional C# host requires the .NET **10** SDK. Normal C++ builds consume checked-in bindings and need no libclang.

Clone `dev-VRI` with `--recurse-submodules`, or run `git submodule update --init --recursive` in an existing
checkout. The default source is the matching commit pinned at `external/libvultra`. For coordinated development,
`--libvultra_dir=../libvultra` explicitly selects a compatible sibling checkout. The engine derives library
requirements from `vultra_features`; do not also override `libvultra_features` or copy library sources here.

## Configure

From the repository root:

```sh
# Minimal CPU world and its tests.
xmake f -m release --vultra_features=world -y
xmake build --all
xmake test -v

# Full development configuration, including graphics and language hosts.
xmake f -m release --vultra_features=all -y
xmake build --all
xmake build vultra-scripting vultra-managed-host
```

The default is `all`. Select comma-separated capabilities:

| Capability | Adds |
| --- | --- |
| `world` | Identity, hierarchy, components, resources, properties, persistence and execution. |
| `asset_import`, `scene_prepare` | CPU model import and GPU-upload preparation, selected separately. |
| `rendering`, `shader` | Prepared raster rendering and source shader compilation, selected separately. |
| `desktop`, `editor_gui`, `vgui` | Window services, debug/property UI and document-style game UI. |
| `script_native`, `script_lua`, `script_csharp` | Optional hosts; scripts can run without graphics or GUI. |
| `export` | Export implementation and its import/cooking dependencies; no standalone exporter target yet. |

For example, `world,script_native,script_lua,script_csharp` builds the three hosts without rendering/UI.
`world,asset_import` permits CPU import without a GPU. Editor/Player/sample targets are not wired yet.

## Tests and generated code

```sh
# Full selection: nine CPU cases and one finite offscreen GPU synchronization case.
env -u DISPLAY -u WAYLAND_DISPLAY xmake test -v -j1

# Complete API generation needs rendering and editor_gui in the selected configuration.
xmake project -k compile_commands .
xmake codegen --check
clang-format --dry-run --Werror path/to/changed.cpp
clang-tidy -p . path/to/changed.cpp
```

On Windows, run tests directly without the POSIX `env` prefix. The current tests are windowless.
The `world` selection registers seven CPU tests; CPU import and full graphics add their own tests.
Many migrated script/extension/editor fixtures are not registered yet. Their presence is not test acceptance.

The codegen task invokes the shared library generator with `source/api/codegen.json`. Regeneration requires
Python, matching libclang and clang-format; ordinary builds do not. Restore a full compilation database after
CPU-only or cross-platform checks. Roslyn generates C# user-script exports separately from the host SDK.

## Changing machines

Clone `dev-VRI` recursively, or pull it and run `git submodule update --init --recursive`. Read
[handoff](docs/ai/handoff.md), then configure and rebuild. Do not carry `.xmake`, compilation databases,
managed `bin/obj`, package caches or native binaries to another OS. No local source archive is required.

Linux Vulkan/offscreen checks are the current local evidence. Windows/D3D12/Metal hardware, clean-machine
Player/export delivery and physical Quest acceptance remain separate gates. Keep temporary output in
`build/.tmp/`; prefer finite offline runs for QA.
