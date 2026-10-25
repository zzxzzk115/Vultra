# Roadmap

Vultra is a C++23 game engine with an optional editor and Player. Prioritize explicit ownership, an authoritative
EnTT world, stable authoring/resource contracts and safe Native/Lua/C# APIs before expanding editor panels or
language hosts. [architecture.md](architecture.md) defines the subsystem and lifecycle contracts.

Godot, Flax, Bevy, Mainframe and OpenEngine inform specific mechanisms; the architecture records pinned sources.
VRI remains the graphics boundary and RenderGraph the only executor. libvultra is the platform/graphics/GUI
dependency; the engine owns world, project, script and editor policy.

## Present implementation and gaps

The current dev-VRI build includes `vultra-engine`, optional `vultra-scripting` and the .NET 10 host. Nine engine CPU
test targets are wired. EnTT-backed Node facades, generated property/C tables, Native/Lua/C# hosts, script
attachments and staged replacement exist. Additional tests, examples, export tools and Player/editor targets
are not yet wired into this branch. Legacy editor/hub code remains separate and unintegrated.

The build derives library requirements from explicit `vultra_features`. `world` passes seven CPU contracts and
checks its actual dependency closure for absent graphics, UI, VM, compiler and importer packages. CPU import is a
separate addition; GPU synchronization and all three language hosts also build separately. The full configuration
passes nine CPU tests and an offscreen forward/deferred material/transform synchronization regression. Further
source relocation and Player/editor/export integration remain incomplete.

The shared libclang generator now discovers domains from attributes, honors compile-command directories and
centralizes transport/borrow metadata and table fingerprints. Host-specific member/SDK policy remains.
`VultraBindings.g.cs` describes interop layouts; safe native-class wrappers, `ScriptScene` calls and native-type
dispatch are mostly handwritten. The Roslyn generator already emits user-script identities/exports/accessors.
That does not generate the engine SDK or establish AOT compatibility.

Resources use document-local `ResourceId`, optional source `AssetId` and live `ObjectId`. Shallow resource
duplication, independent mutation, persistence and retired runtime identities have CPU regressions. E4 still
needs nested-scene identity remapping, external resource loading/cache policy and document-level MakeUnique.

## Design contracts to settle first

| Scope | Authority and lifetime |
| --- | --- |
| Library context | Explicit GPU/platform/GUI services. No world, script VM, editor document or process-global type registry is required by libvultra. |
| Project | Asset identity/dependencies, type catalog and loaded type modules. Installation is explicit; borrowed factories/accessors keep their module loaded. Multiple projects must not share mutable registration state. |
| World | EnTT registry, runtime identity/hierarchy/components, clock/input, resource instances and script instances. Node is an optional authoring/script facade; runtime-only entities need no Node. Detached Nodes own staging values that transfer on attachment, never a per-frame mirror. |
| Document | Authored scene/resource data, revision, selection and undo/redo. Edit operations publish validated changes. Play owns a separate world; stopping Play never serializes simulation mutations into the document. |
| View | Camera/output selection, GPU targets/history and input ownership. World state does not own a window or GUI. Render data is derived through change revisions and the library's existing server/direct paths. |

World identity and hierarchy change through validated world operations. Native code may query EnTT directly and
patch its supported components in batches, with revision notification; it cannot bypass identity, ownership or
deferred-structural-change invariants. Property setters use the same validation/notification path. Reflection is
metadata and semantic access, not permission to overwrite arbitrary component bytes.

### Persistence and scene instantiation

Keep UTF-8 JSON and every file/schema/ABI version at **1**. Update both ends with deliberate breaking changes;
do not copy Mainframe/Godot compatibility readers or per-type migrations.

- Project `AssetId` identifies a durable asset independently of its path. Runtime `ObjectId` identifies a live
  object in one world; EnTT entity slots, GPU RIDs and graph handles are never serialized.
- A scene definition owns stable node/local-resource identities. Nested instances need a mapping from the
  instance chain plus definition-local identity to live objects. Two instances of one PackedScene must resolve
  the same source-node ID to different runtime objects. External resource references name asset IDs; local ones
  name document-local resources. The schema supplies expected type/category and validates the resolved target.
- Parent, authoring owner and reference are different relationships. Persisted owner determines scene membership;
  a reference does not silently adopt its target. A cached source asset and a mutable world resource instance are
  distinct; shared references reuse the instance, while explicit MakeUnique creates independent editable data.
- Load in stages: parse/version/schema validation; create identities; populate values; resolve references and
  instance overrides; validate ownership/dependency cycles; attach scripts; enter execution. Loading failure must
  not publish half a world or replace the preceding valid document. Reject unknown required types/capabilities
  with a source/type/property diagnostic; missing-type placeholders are not currently implemented.
- Preserve explicit defaults and property visibility flags through save/reopen. Sparse instance overrides use
  stable member/node identities, not child indices or UI labels. A deleted/replaced target never revives an old
  runtime handle merely because its persistent ID is reused.

Standalone resource documents, nested PackedScene, resource-instance identity and overrides are still E4 work.
Agree on these contracts before E3 history stores reference/topology operations; full E4 functionality can follow
the first document-edit slice.

### Generated host SDK and script metadata

Use one libclang frontend/IR for the annotated host API. Native declarations provide host type, base, member/property role,
canonical value types, owning domain/context, call phase/thread, borrow/output lifetime and source diagnostics.
Generate C tables/thunks and safe Native/C#/Lua SDK access from this model. Engine classification and factories
belong here; library descriptors remain independent of Node/Resource/ECS semantics.

The first E0 completion fixture adds an annotated Node3D property and a method, then exercises inherited access
from an independently compiled C-only extension, a C++ script, Lua and safe C#. A supported addition must require
no per-function emitter branch, wrapper or native-type lookup entry. Generated setters call the semantic native
setter. Typed vector/scalar access uses typed ABI values, not JSON; generic Inspector/document operations can
continue to use dynamic property values. Test exact layouts, status conversion, table size/signature fingerprint,
wrong context, stale IDs and unsupported signatures. All ABI versions remain 1.

The generated SDK owns mechanical member/type dispatch. A small handwritten runtime owns lifetime, host bootstrap,
identity resolution and truly custom conversions. User scripts never write unsafe interop or plugin entry bodies.
Ordinary C++ applications retain direct native access without the ABI or scripting host. Dynamic native modules
link the generated C-facing SDK, not a second static copy of the engine/library.

Roslyn separately generates C# user-script `[Export]` access and class/callback metadata. Lua's explicit class and
export declarations and native script descriptors supply the same schemas. All hosts use the same object IDs,
property validation and lifecycle; registering a language must not add another object model. C++26 reflection
remains an experiment, not an additional production pipeline.

### Execution, reload and edit operations

Keep the existing enter/ready/exit, fixed/variable update and input phases. GUI construction occurs only in its GUI
phase. Structural mutations during dispatch are deferred; stopped/queued-for-deletion owners receive no further
callbacks. Define signal ordering and reentrancy in E2: ordered dispatch on the owning thread, disconnection
takes effect before the next call, and newly connected listeners wait for a subsequent emission. Do not create a
global event bus or an unmeasured job scheduler.

Reload stages the whole dependency-connected group, validates schemas/references and installs candidates before
retiring the old group. Failure keeps the preceding valid group; arbitrary script side effects cannot be rolled
back. Stop callbacks and clear borrowed frames, descriptors, delegates, GUI registrations and managed wrapper
caches before unloading code. Native registered types remain non-reloadable while live/borrowed. Verify .NET
collectible AssemblyLoadContext release with weak references and diagnostics; replacement success alone does
not prove unload. Keep authored exports separate from transient reload state.

Document operations own validation, revision checks, results and history. GUI and headless callers use those same
handlers. A drag applies values immediately; begin/change/commit/cancel identify one gesture and one undo entry.
Failed composite edits roll back document data; deleted subtrees/resources remain history-owned until pruned.
An optional CLI/MCP adapter later invokes these operations without becoming a library or Player dependency.

## Implementation sequence and acceptance

| Gate | Existing base | Next deliverable and acceptance |
| --- | --- | --- |
| Core — dependency isolation | Minimal CPU world, CPU import, optional language hosts and full graphics selections build independently. Nine CPU tests and one offscreen GPU synchronization test pass. | Finish owning source/test/tool migration and test other platforms. Preserve explicit UI, language-host, renderer and tool requirements. Only remove old copies after owner tests pass. |
| E0 — types and generated SDK | Generated native properties/C tables, context-owned catalogs and Roslyn exports. | Normalize host IR and emit safe classes/member dispatch. Complete the one-property/one-method fixture above; cover defaults, inherited fields, typed references, enum/array validation, diagnostics and independent contexts. No global auto-registration. |
| E1 — script attachments | Stable module asset IDs, per-node class/exports and staged Native/Lua/C# attachment/replacement. | Wire migrated tests/examples; edit two independent instances, save/reopen and reload through the same schema. Ordered type-module installation fails atomically and retires in reverse order. Registered-type hot reload stays unsupported. |
| E2 — execution | EnTT lifecycle, clocks/pause/step, priorities, input and deterministic named RNG. | Complete owner-scoped signals, deferred-deletion and mixed-language input/callback tests, plus project input configuration and registered-type notifications. Test batch component updates and next-frame rendering notification. |
| E3 — documents and editor UI | Existing generated Inspector and staged scene/graph edits are building blocks. | Property/topology/attachment operations, undo/redo and revisions, presented through retained controls/PropertyPresenter. GUI/headless parity, live drag/one undo/cancel, failed composite edit and independent Edit/Play ownership. Native GUI must not depend on the engine or .NET. |
| E4 — resources and PackedScene | Inline typed resources, references, authored snapshots and VPK byte sources. | Implement the instantiation/persistence contracts above: standalone resources, nested templates, sparse overrides and MakeUnique. Two instances/two worlds, forward references, dependency cycles, deletion, malformed input and save/reopen. Complete Lua stream loading and cooked engine bootstrap without whole-package extraction. |
| E5 — views/game UI | Raster synchronization, output probes and ordinary VGui controls. | Independent views/history and picking; RmlUi transforms/masks/layers/effects have their own capability/interaction/pixel/teardown gates. Native editor GUI, optional ImGui debug UI and RmlUi game UI remain distinct. |
| E6 — independent Play | Offline rendering exists; browser streaming does not. | Player/world start, pause/step, stop and logs preserve the authored document. Optional preview is bounded to 15 fps/two queued frames, drops oldest and cancels with its owner; visible windows are explicit. |
| E7 — delivery | Earlier pack/export APIs, embedded VPK and no-xmake prototypes remain migration inputs. | Integrate Player/tools/export templates; verify clean Linux/Windows delivery. Base Player is single EXE with embedded VPK, no editor/importer/compiler requirements. Static Lua may read packed source; optional .NET/native modules declare runtime/sidecar payloads. Existing WASM/AOT probes do not establish supported AOT shipping. |

Execute dependency isolation → E0 → E1/E2 with the E4 format contract → E3, then finish E4–E7. This is a dependency order, not
permission to replace already working subsystems or require a complete editor before direct engine use.
Desktop/Web development has priority. Preserve Android ARM64 and candidate iOS/iPadOS compile/link gates;
mobile editor interaction, real Quest and hardware-specific backend acceptance remain later independent gates.

Each stage needs owning targets/tests, deterministic codegen, formatting/static analysis and failure/recovery
coverage. Rendering stages also need finite offline runs, GPU diagnostics and final/intermediate image inspection.
Prefer offline QA; explicit visible testing uses Workspace 5. Documentation-only reviews do not run GPU tests.
Physics, animation and audio remain optional ECS/server adapters with explicit fixed-step, transform and
resource-retirement contracts. Jolt, Ozz and miniaudio are candidates, not core dependencies.
