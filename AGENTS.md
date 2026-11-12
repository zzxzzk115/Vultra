# Vultra Editor Development Rules

This branch holds the editor shell only. The rules of
[libvultra's AGENTS.md](https://github.com/zzxzzk115/libvultra/blob/dev-slim/AGENTS.md) apply here as well:
solve the current task, keep one clear implementation, return `std::expected<T, vultra::Error>` instead of
throwing, follow `.clang-format` and `.clang-tidy`, write everything in English and report what was verified.

Specific to this repository:

- The editor is a front end of `vultra-host` from Vultra-CLI. Loading, validation, reloading, the frame graph
  and diagnostics belong to the host; the editor shows them and does not reimplement them. A feature the
  command line also needs goes into the host first.
- Build UI in `onImGui()`, change the session between completed frames in `onUpdate()`, record in
  `onRender()`. The GUI lets go of the session's display image before the session replaces it.
- Do not bring code back from `dev-VRI` wholesale. A piece that is needed arrives as a package or as a small,
  reviewed addition.
- Temporary verification output goes to the ignored `build/.tmp/`.
