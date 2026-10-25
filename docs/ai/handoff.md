# Handoff

## Checkout and build

Continue on `dev-VRI`. Clone with its pinned library dependency:

```sh
git clone --branch dev-VRI --recurse-submodules https://github.com/zzxzzk115/Vultra.git
cd Vultra
xmake f -m release --vultra_features=world -y
xmake build --all
xmake test -v
```

An existing checkout needs `git pull --ff-only` and `git submodule update --init --recursive`.
The library pin is `5dd86f46`; no sibling checkout or local archive is required. [BUILD.md](../../BUILD.md)
has full-host/test/codegen commands. Reconfigure on the destination machine; do not transfer caches or binaries.
The default pinned-submodule CPU build and its seven tests also pass without a sibling override.

## Working implementation

The active build owns `vultra-engine`, the header-only native SDK, optional `vultra-scripting` and the .NET 10
managed host. World/resource persistence, EnTT authority, generated property/C tables, Native/Lua/C# lifecycle
and staged replacement are implemented. ResourceId is mutable authored identity, AssetId is source provenance,
and ObjectId is live world identity. Resource duplication is shallow and generates new identities.

The full configuration has nine CPU tests and one offscreen forward/deferred scene-sync test. The minimal world
passes seven CPU tests without VRI/UI/VM/import/compiler packages; CPU import adds two relevant contracts.
All three language-host implementations build together without graphics/UI. This does not mean all migrated
language/reload fixtures run in the current target set. Host SDK access and native dispatch remain partly
handwritten; generated C layouts and Roslyn user-script exports do not complete SDK generation or AOT.

## Migration still to finish

- `tests/xmake.lua` is the active test inventory. Additional script/extension/input/reload fixtures need owning
  target wiring and failure/recovery checks before claiming their earlier acceptance here.
- `runtime/`, `tools/`, `examples/` and `editor/` are not included by the current root build. Integrate engine
  Player/export/scripting consumers after checking their updated library API requirements.
- The copied research workbench, `extensions/research` and research Python interface are still unintegrated.
  Extract engine-independent policy into VultraLab; keep only a concrete optional engine adapter here.
  Do not present the old combined workbench as the new editor or restore research dependencies to the library.
- Earlier editor/hub sources from master are preserved. Review useful behavior individually; do not overwrite
  the current EnTT core with that implementation. Redundant copied library examples/check drivers are migration
  inputs until their relevant engine fixtures have replacements, not another supported library build.

## Next engine slice

Finish generic host SDK generation with one annotated property/method used by C, C++, Lua and safe C#.
Then complete E1/E2 test wiring and owner-scoped signals/input, followed by document operations/history and
independent Play. Resource instancing/remapping/MakeUnique and Player/cooked delivery remain open.
Native retained GUI is not implemented. Follow [roadmap](../roadmap.md); do not mark the whole core complete.

Keep callbacks/registrations alive until worlds, facades, EnTT pools and GPU consumers retire. Native registered
classes cannot hot-reload while borrowed. Lua cache rollback is shallow; arbitrary script side effects are not
transactional. Managed collective reload does not establish AOT. All file/ABI versions remain **1**.

## Evidence and limits

Earlier focused logs are on the original machine under libvultra's `build/.tmp/core-boundaries`;
`build/.tmp/render-graph-core/engine-test.log` verifies the downstream GPU-sync consumer after the graph change.
The ten registered tests passed during handoff cleanup; logs on the original machine are optional evidence.
No new Windows, Metal, browser, mobile or physical-headset acceptance is claimed. Use offline QA;
visible testing requires Workspace 5. Update dependent pins together after later library changes.
