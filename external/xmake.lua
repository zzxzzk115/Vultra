-- NOTE: Renderdoc is not supported on Wayland, so we disable it when Wayland is enabled.
disable_renderdoc = is_plat("linux") and is_config("wayland")
set_config("examples", is_config("examples"))
set_config("tests", is_config("tests"))
set_config("renderdoc", not disable_renderdoc)
includes("libvultra")