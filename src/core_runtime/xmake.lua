-- add requirements
add_requires("FrameGraph")

add_requires("spdlog", "fmt", "magic_enum", "entt", "glm", "stb", "vulkansdk", "vulkan-memory-allocator-hpp", "openxr-sdk")
add_requires("glfw", {configs = {wayland = true}})
add_requires("tracy 0.11.1", {configs = {
    on_demand = true,
    enforce_callstack = true,
    callstack = true,
    broadcast = true,
    code_transfer = true,
    context_switch = true,
    exit = true,
    sampling = true,
    verify = true,
    vsync_capture = true,
    frame_image = true,
    system_tracing = true,
    crash_handler = true
}})
add_requires("imgui", {configs = { vulkan = true, sdl3 = true, wchar32 = true}})
add_requires("libsdl3", {configs = {shared = true, debug = is_mode("debug"), vulkan = true }})
add_requires("jsoncpp", {configs = {shared = true, debug = is_mode("debug")}})
add_requires("assimp", {configs = {shared = true, debug = is_mode("debug")}})
add_requires("spirv-cross", {configs = {shared = true, debug = is_mode("debug")}})
add_requires("glslang", {configs = {shared = true, debug = is_mode("debug")}})


-- target defination, name: vultra
target("vultra")
    -- set target kind: static library
    set_kind("static")

    -- add include dir
    add_includedirs("include", {public = true}) -- public: let other targets to auto include

    -- add header files
    add_headerfiles("include/(vultra/**.hpp)")

    -- add source files
    add_files("src/**.cpp")

    -- add packages
    add_packages("FrameGraph", { public = true })
    add_packages(is_mode("debug") and "openxr-loader-debug" or "openxr-loader", { public = true })
    add_packages("spdlog", "fmt", "magic_enum", "entt", "glm", "stb", "vulkansdk", "vulkan-memory-allocator-hpp", "openxr-sdk", { public = true })
    add_packages("tracy", "imgui", "libsdl3", "jsoncpp", "assimp", "spirv-cross", "glslang", { public = true })

    -- linux workaround for spirv-cross linking
    if is_plat("linux") then
        add_links("spirv-cross-core", "spirv-cross-glsl")
    end

    -- tracy & tracky required defines
    -- default: tracy enabled, tracky disabled
    add_defines("TRACY_ENABLE=1", { public = true })
    add_defines("TRACKY_ENABLE=0", { public = true })
    add_defines("TRACKY_VULKAN", { public = true })

    -- fmt fix
    add_defines("FMT_UNICODE=0", { public = true })

    if is_mode("debug") then
        add_defines("_DEBUG", { public = true })
    else
        add_defines("NDEBUG", { public = true })
    end

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/vultra")
