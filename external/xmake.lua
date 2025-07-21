add_rules("mode.debug", "mode.release")

package("FrameGraph")
    add_deps("cmake")
    set_sourcedir(path.join(os.scriptdir(), "FrameGraph"))
    on_install(function (package)
        local configs = {}
        table.insert(configs, "-DCMAKE_BUILD_TYPE=" .. (package:debug() and "Debug" or "Release"))
        table.insert(configs, "-DBUILD_SHARED_LIBS=" .. (package:config("shared") and "ON" or "OFF"))
		table.insert(configs, "-DFG_BUILD_TEST=OFF")
        import("package.tools.cmake").install(package, configs)
    end)
package_end()