# Vultra

A C++23 engine built on libvultra. The `dev-VRI` branch is an early engine-core migration: an EnTT world,
Node/Resource authoring, JSON persistence, generated C interfaces and optional Native C++/Lua/C# scripting.
A CPU world needs no window, GPU, GUI or language VM.

The current targets are `vultra-engine`, `vultra-native-sdk`, optional `vultra-scripting` and the .NET 10
`vultra-managed-host`. Editor, Player, export tools and the scripting showcase are not integrated into the
current build. The earlier editor/hub sources remain migration inputs; their old screenshots and commands
are not instructions for this branch.

## Build the core

Install xmake and a C++23 compiler, then clone the branch with its pinned library dependency:

```sh
git clone --branch dev-VRI --recurse-submodules https://github.com/zzxzzk115/Vultra.git
cd Vultra
```

```sh
xmake f -m release --vultra_features=world -y
xmake build --all
xmake test -v
```

The default build uses `external/libvultra`. Coordinated development can select a sibling checkout with
`--libvultra_dir=../libvultra`.
See [BUILD.md](BUILD.md) for graphics, language hosts, generation and test commands.
[Architecture](docs/architecture.md) defines ownership and formats; [roadmap](docs/roadmap.md) lists the remaining
engine gates. Migration state and cross-machine notes are in [handoff](docs/ai/handoff.md).

## Design

- EnTT owns runtime entities/components. Node is an authoring and scripting facade, with no per-frame mirror.
- Projects own asset/type catalogs; worlds own live identity, hierarchy, resources and execution.
- Native, Lua and safe C# use the same property schemas, object IDs and lifecycle.
- Documents will own undo/history and a separate Play world. Those editor workflows are still planned.
- All engine file/ABI versions remain **1** before release; format changes update both readers and writers.

VRI remains the graphics boundary. Platform, rendering and optional GUI come from one libvultra dependency;
research experiments and pass collections are separate products.

## License

[MIT](LICENSE). Third-party code and assets retain their own notices.
