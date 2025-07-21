-- add requirements
add_requires("FrameGraph")

add_requires("vcpkg::sdl3[vulkan]", {configs = {shared = true}, alias = "sdl3"})
add_requires("vcpkg::imgui[docking-experimental,sdl3-binding,vulkan-binding,wchar32]", { alias = "imgui" })
add_requires("vcpkg::jsoncpp", {configs = {shared = true, debug = is_mode("debug")}, alias = "jsoncpp"})
add_requires("vcpkg::openxr-loader", {configs = {shared = true, debug = is_mode("debug")}, alias = "openxr-loader"})
add_requires("vcpkg::assimp", {configs = {shared = true, debug = is_mode("debug")}, alias = "assimp"})
add_requires("vcpkg::spirv-cross", {configs = {shared = true, debug = is_mode("debug")}, alias = "spirv-cross"})
add_requires("vcpkg::glslang", {configs = {shared = true, debug = is_mode("debug")}, alias = "glslang"})

add_requires("spdlog", "fmt", "magic_enum", "entt", "tracy", "glm", "stb", "vulkansdk", "vulkan-memory-allocator-hpp")

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
    add_packages("sdl3", "imgui", "jsoncpp", "openxr-loader", "assimp", "spirv-cross", "glslang", { public = true })
    add_packages("spdlog", "fmt", "magic_enum", "entt", "tracy", "glm", "stb", "vulkansdk", "vulkan-memory-allocator-hpp", { public = true })

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