#include "editor.hpp"
#include "editor_app.hpp"

#include <vultra/app/packaged_resources.hpp>
#include <vultra/core/base/command_line.hpp>
#include <vultra/host/resources.hpp>

#include <iostream>

namespace vultra::editor
{
    namespace
    {
        std::expected<int, Error> run(const host::Registry& registry, Options options)
        {
            const auto resources = PackagedResources::create();
            if (!resources)
            {
                return std::unexpected(resources.error());
            }
            const auto shaders = AssetSource::create(resources->engineRoot());
            if (!shaders)
            {
                return std::unexpected(shaders.error());
            }
            // The docking layout belongs to the user, not to one project: the editor opens other projects.
            if (options.layout.empty())
            {
                const auto user = host::userDirectory();
                if (!user)
                {
                    return std::unexpected(user.error());
                }
                options.layout = *user / "editor.ini";
            }
            EditorGuiConfig gui;
            gui.iniFile = options.layout;
            AppConfig config;
            config.title  = "Vultra Editor";
            config.size   = {1600, 900};
            auto services = ImGuiApp::createServices(config, gui);
            if (!services)
            {
                return std::unexpected(services.error());
            }
            const auto app = EditorApp::create(std::move(*services), registry, options, **shaders);
            if (!app)
            {
                return std::unexpected(app.error());
            }
            if (auto finished = (*app)->run(options.frames); !finished)
            {
                return std::unexpected(finished.error());
            }
            return (*app)->finish();
        }
    } // namespace

    int commandLine(const host::Registry& registry, int argc, char** argv)
    {
        argparse::ArgumentParser parser("vultra-editor", "0.1.0", argparse::default_arguments::none);
        parser.add_description("Open a Vultra project in the editor.");
        addAppOptions(parser);
        parser.add_argument("--project").help("project directory or .vproject file; without it the editor is empty");
        parser.add_argument("--debug").help("use the project's debug profile (the default)").flag();
        parser.add_argument("--release").help("use the project's release profile").flag();
        parser.add_argument("--releasedbg").help("use the project's releasedbg profile").flag();
        parser.add_argument("--viewports")
            .help("viewports to open, instead of those the project had")
            .scan<'u', uint32_t>();
        parser.add_argument("--layout").help("file the docking layout is kept in, instead of the user's");
        parser.add_argument("--capture").help("save the window of the last frame (the first without --frames)");
        const auto parsed = parseCommandLine(parser, argc, argv);
        if (!parsed)
        {
            std::cerr << "vultra-editor: " << parsed.error().message << '\n';
            return 2;
        }
        if (!*parsed)
        {
            return 0;
        }
        const int modes = int(parser.get<bool>("--debug")) + int(parser.get<bool>("--release")) +
                          int(parser.get<bool>("--releasedbg"));
        if (modes > 1)
        {
            std::cerr << "Choose one of --debug, --release and --releasedbg\n";
            return 2;
        }
        Options options;
        options.project = parser.present<std::string>("--project").value_or("");
        if (parser.get<bool>("--release"))
        {
            options.mode = host::Mode::eRelease;
        }
        if (parser.get<bool>("--releasedbg"))
        {
            options.mode = host::Mode::eReleaseDebug;
        }
        options.frames    = parser.present<uint64_t>("--frames").value_or(0);
        options.viewports = parser.present<uint32_t>("--viewports");
        options.layout    = parser.present<std::string>("--layout").value_or("");
        options.capture   = parser.present<std::string>("--capture").value_or("");
        const auto code   = run(registry, std::move(options));
        if (!code)
        {
            std::cerr << "vultra-editor: " << code.error().message << '\n';
            return 1;
        }
        return *code;
    }
} // namespace vultra::editor
