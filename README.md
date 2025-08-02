# Vultra

<h4 align="center">
  Vultra is a modern 3D engine for game development, graphics research and more.
</h4>

<p align="center">
    <a href="https://github.com/zzxzzk115/Vultra/actions" alt="Build-Windows">
        <img src="https://img.shields.io/github/actions/workflow/status/zzxzzk115/Vultra/build_windows.yaml?branch=master&label=Build-Windows&logo=github" /></a>
    <a href="https://github.com/zzxzzk115/Vultra/actions" alt="Build-Linux">
        <img src="https://img.shields.io/github/actions/workflow/status/zzxzzk115/Vultra/build_linux.yaml?branch=master&label=Build-Linux&logo=github" /></a>
    <a href="https://github.com/zzxzzk115/Vultra/actions" alt="Build-macOS">
        <img src="https://img.shields.io/github/actions/workflow/status/zzxzzk115/Vultra/build_macos.yaml?branch=master&label=Build-macOS&logo=github" /></a>
    <a href="https://github.com/zzxzzk115/Vultra/issues" alt="GitHub Issues">
        <img src="https://img.shields.io/github/issues/zzxzzk115/Vultra">
    </a>
    <a href="https://github.com/zzxzzk115/Vultra/blob/master/LICENSE" alt="GitHub">
        <img src="https://img.shields.io/github/license/zzxzzk115/Vultra">
    </a>
</p>

(This project is under early development and WIP.)

Vultra is based on [libvultra](https://github.com/zzxzzk115/libvultra).

## Features
You may also want to know the core features [here](https://github.com/zzxzzk115/libvultra?tab=readme-ov-file#features)

- Modern Rendering System on top of libvultra.

## Build Instructions

Prerequisites:
- Git
- XMake
- Vulkan SDK
- Visual Studio with MSVC if Windows
- GCC or Clang if Linux/Unix
- XCode with GCC or Apple Clang if macOS

Step-by-Step:

- Install XMake by following [this](https://xmake.io/guide/quick-start.html#installation). 

- Clone the project:
  ```bash
  git clone https://github.com/zzxzzk115/Vultra.git
  ```

- Build the project:
  ```bash
  cd Vultra
  xmake -vD
  ```

- Run the programs:
  ```bash
  xmake run
  ```
  or run a specific program:
  ```bash
  xmake run VultraEditor
  xmake run VultraHub
  ```

  > **Tips:**
  > For OpenXR programs, you may need to set the XR_RUNTIME_JSON environment variable.
  > For debugging OpenXR programs without headsets, you may need Meta XR Simulator on Windows and macOS. On Linux, you can use Monado as the simulator.

## Future Work
- [ ] Modern Render Paths
- [ ] Resource Pipeline
- [ ] Project Management
- [ ] Hot-Reload

## License
This project is under the [MIT](LICENSE) license.