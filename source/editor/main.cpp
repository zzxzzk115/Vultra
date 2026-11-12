#include "editor_app.hpp"

#include <vultra/app/packaged_resources.hpp>
#include <vultra/core/base/command_line.hpp>

#include <vultra_lua/scripting.hpp>

#include <iostream>

namespace
{
    using vultra::Error;
    using vultra::editor::Options;
    namespace host = vultra::host;

    std::expected<void, Error> run(const Options& options)
    {
        // The editor carries the scripting package, like the stock command line.
        host::Registry registry;
        if (auto registered = vultra_lua::registerPackage(registry); !registered)
        {
            return registered;
        }
        registry.addPackage(std::string(vultra_lua::kExtensionId));

        host::Diagnostics diagnostics;
        const auto        workspace = host::loadWorkspace(options.project, registry, diagnostics);
        if (!workspace)
        {
            return vultra::makeError("{}", diagnostics.text(options.project));
        }
        const auto resources = vultra::PackagedResources::create();
        if (!resources)
        {
            return std::unexpected(resources.error());
        }
        const auto shaders = vultra::AssetSource::create(resources->engineRoot());
        if (!shaders)
        {
            return std::unexpected(shaders.error());
        }
        // The docking layout belongs to the project.
        vultra::EditorGuiConfig gui;
        gui.iniFile = workspace->project.directory / ".vultra" / "editor.ini";
        vultra::AppConfig config;
        config.title      = "Vultra Editor | " + workspace->project.name;
        config.size       = {1600, 900};
        config.validation = workspace->profile().validation;
        auto services     = vultra::ImGuiApp::createServices(config, gui);
        if (!services)
        {
            return std::unexpected(services.error());
        }
        const auto app =
            vultra::editor::EditorApp::create(std::move(*services), registry, *workspace, options, **shaders);
        if (!app)
        {
            return std::unexpected(app.error());
        }
        return (*app)->run(options.frames);
    }
} // namespace

int main(int argc, char** argv)
{
    argparse::ArgumentParser parser("vultra-editor", "0.1.0", argparse::default_arguments::none);
    parser.add_description("Open a Vultra project in the editor.");
    vultra::addAppOptions(parser);
    parser.add_argument("project").help("project directory or .vproject file").default_value(std::string("."));
    parser.add_argument("--capture").help("save the window of the last frame (the first without --frames)");
    parser.add_argument("--viewports")
        .help("viewports open at the start")
        .scan<'u', uint32_t>()
        .default_value(uint32_t(1));
    const auto parsed = vultra::parseCommandLine(parser, argc, argv);
    if (!parsed)
    {
        std::cerr << "vultra-editor: " << parsed.error().message << '\n';
        return 2;
    }
    if (!*parsed)
    {
        return 0;
    }
    const Options options {parser.get<std::string>("project"),
                           parser.present<uint64_t>("--frames").value_or(0),
                           parser.present<std::string>("--capture").value_or(""),
                           parser.get<uint32_t>("--viewports")};
    if (auto result = run(options); !result)
    {
        std::cerr << "vultra-editor: " << result.error().message << '\n';
        return 1;
    }
    return 0;
}
