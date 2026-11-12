# Vultra Editor Development Rules

This branch holds the editor shell only. The rules of
[libvultra's AGENTS.md](https://github.com/zzxzzk115/libvultra/blob/dev-slim/AGENTS.md) apply here as well:
solve the current task, keep one clear implementation, return `std::expected<T, vultra::Error>` instead of
throwing, follow `.clang-format` and `.clang-tidy`, write everything in English and report what was verified.

Specific to this repository:

- The editor is a front end of `vultra-host` from Vultra-CLI. Loading, validation, reloading, the frame graph
  and diagnostics belong to the host; the editor shows them and does not reimplement them. A feature the
  command line also needs goes into the host first.
- Build UI in `onImGui()`. Everything that changes or renders the session happens in `onUpdate()`, between
  completed frames: reloads, resizes and one recording per viewport into that viewport's image. `onRender()`
  draws the GUI only. The GUI lets go of a viewport's image before it is destroyed.
- A window is docked through the default layout in `defaultLayout()`; give a new window a place there and an
  entry in the `View` menu.
- What the editor keeps for a project goes into the project's `.vultra/editor.json`; the docking layout
  belongs to the user. Tests pass `--layout` and never write the user's layout.
- Behaviour that can be observed from outside gets a case in `tests/run.lua`.
- Do not bring code back from `dev-VRI` wholesale. A piece that is needed arrives as a package or as a small,
  reviewed addition.
- Temporary verification output goes to the ignored `build/.tmp/`.
