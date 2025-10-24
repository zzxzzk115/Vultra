add_requires("argparse")

target("VultraEditor")
    -- set kind: binary
    set_kind("binary")

    -- add include dir
    add_includedirs("include", {public = true}) -- public: let other targets to auto include

    -- add header files
    add_headerfiles("include/(vultra_editor/**.hpp)")

    -- add source files
    add_files("src/**.cpp", "imgui.ini")

    -- add packages
    add_packages("argparse", {public = true})

    -- add deps
    add_deps("VultraEngine")

    -- add rules
    add_rules("linux.sdl.driver", "imguiconfig")

    -- set run arguments
    set_runargs("--project", "$(projectdir)/example_project/ExampleProject.vproj")

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/VultraEditor")