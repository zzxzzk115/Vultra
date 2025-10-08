target("VultraEditor")
    -- set kind: binary
    set_kind("binary")

    -- add include dir
    add_includedirs("include", {public = true}) -- public: let other targets to auto include

    -- add header files
    add_headerfiles("include/(vultra_editor/**.hpp)")

    -- add source files
    add_files("src/**.cpp")

    -- add deps
    add_deps("vultra")

    -- add rules
    add_rules("linux.sdl.driver")

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/VultraEditor")