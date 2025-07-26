-- add requirements
add_requires("fmt", { system = false })
add_requires("spdlog", "magic_enum", "entt", "glm", "stb", "vulkansdk", "vulkan-memory-allocator-hpp", "fg")
add_requires("tracy 0.11.1", {configs = {on_demand = true}})
add_requires("imgui v1.92.0-docking", {configs = { vulkan = true, sdl3 = true, wchar32 = true}})
add_requires("assimp", {configs = {shared = true, debug = is_mode("debug")}})
add_requires("spirv-cross", {configs = {shared = true, debug = is_mode("debug")}})
add_requires("glslang", {configs = {shared = true, debug = is_mode("debug")}})
add_requires("openxr", {configs = {shared = true, debug = is_mode("debug")}})

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
    add_packages("fmt", "spdlog", "magic_enum", "entt", "glm", "stb", "vulkansdk", "vulkan-memory-allocator-hpp", "fg", { public = true })
    add_packages("tracy", "imgui", "libsdl3", "assimp", "spirv-cross", "glslang", "openxr", { public = true })

    -- vulkan dynamic loader
    add_defines("VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1", { public = true })

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
