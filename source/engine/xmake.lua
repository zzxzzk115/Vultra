target("VultraEngine")
    -- set kind: static library
    set_kind("static")

    -- add include dir
    add_includedirs("include", {public = true}) -- public: let other targets to auto include

    -- add header files
    add_headerfiles("include/(vultra_engine/**.hpp)")

    -- add source files
    add_files("src/**.cpp")

    -- add deps
    add_deps("vasset", "vultra", {public = true})

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/VultraEngine")