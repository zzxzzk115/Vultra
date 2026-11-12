#include <vultra/app/camera/orbit_camera.hpp>
#include <vultra/app/imgui_app.hpp>
#include <vultra/app/packaged_resources.hpp>
#include <vultra/assets/asset_source.hpp>
#include <vultra/core/base/command_line.hpp>
#include <vultra/gpu/image_readback.hpp>
#include <vultra/host/runtime.hpp>
#include <vultra/host/shader_pass.hpp>
#include <vultra/platform/os/file_watcher.hpp>

#include <imgui.h>
#include <vultra_lua/scripting.hpp>

#include <algorithm>
#include <iostream>
#include <optional>

// The editor shell: a project in a docked viewport with its pass parameters, its resources and a console.
// It drives the same host runtime as the command line, so a project loads, reloads and fails the same way.
namespace
{
    using vultra::Error;
    using vultra::Extent;
    namespace host = vultra::host;

    struct Options
    {
        std::filesystem::path project;
        uint64_t              frames = 0;
        std::filesystem::path capture;
    };

    struct ResourceRow
    {
        std::string name;
        std::string format;
        std::string semantic;
        std::string provider;
    };

    class EditorApp final : public vultra::ImGuiApp
    {
    public:
        static std::expected<std::unique_ptr<EditorApp>, Error> create(Services                   services,
                                                                       const host::Registry&      registry,
                                                                       const host::Workspace&     workspace,
                                                                       const Options&             options,
                                                                       const vultra::AssetSource& shaders)
        {
            // The viewport decides the size once the first GUI frame has laid it out.
            auto runtime = host::Runtime::create(services.device(),
                                                 registry,
                                                 {{960, 540}, VriFormat_RGBA8_UNORM, &shaders},
                                                 host::Display::eWindow);
            if (!runtime)
            {
                return std::unexpected(runtime.error());
            }
            host::Diagnostics diagnostics;
            if (!(*runtime)->load(workspace, diagnostics))
            {
                return vultra::makeError("{}", diagnostics.text(workspace.project.directory));
            }
            std::unique_ptr<EditorApp> app(
                new EditorApp(std::move(services), registry, workspace, options, std::move(*runtime)));
            if (auto watched = app->m_Watcher.watch(workspace.watchDirectories); !watched)
            {
                return std::unexpected(watched.error());
            }
            app->describe(workspace);
            app->log("loaded " + workspace.project.name);
            return app;
        }

    private:
        EditorApp(Services                       services,
                  const host::Registry&          registry,
                  const host::Workspace&         workspace,
                  const Options&                 options,
                  std::unique_ptr<host::Runtime> runtime) :
            ImGuiApp(std::move(services), {.title = "Vultra Editor | " + workspace.project.name, .size = {1600, 900}}),
            m_Location(workspace.project.file),
            m_Base(workspace.project.directory),
            m_Mode(workspace.mode),
            m_Options(options),
            m_Registry(registry),
            m_Runtime(std::move(runtime))
        {
            const auto& session  = m_Runtime->session();
            m_Camera.center      = session.sceneCenter();
            m_Camera.radius      = session.sceneRadius();
            m_Camera.distance    = session.sceneRadius() * 2.4f;
            m_Camera.verticalFov = glm::radians(45.0f);
        }

        void log(const std::string& text)
        {
            std::cout << "[vultra-editor] " << text << std::endl;
            m_Console.push_back(text);
        }

        // What the resource window lists, kept as text so it does not refer to a workspace that was replaced.
        void describe(const host::Workspace& workspace)
        {
            m_RenderPath = workspace.project.renderPath;
            m_Resources.clear();
            for (const auto& resource : workspace.graph.resources)
            {
                m_Resources.push_back(
                    {resource.name, host::passFormatName(resource.format), resource.semantic, resource.provider});
            }
            // The editor has the packages it was built with; a project's script fails where it uses another.
            for (const auto* package : host::missingNativePackages(workspace, m_Registry))
            {
                log("package " + package->id + " is not part of the editor; `vultra run` builds the project with it");
            }
        }

        // The GUI shows the session's display image; it lets go of it before the session replaces it.
        std::expected<void, Error> releaseDisplay()
        {
            const auto output = m_Runtime->session().output("final");
            if (!output)
            {
                return std::unexpected(output.error());
            }
            return getEditorGui().forgetTexture(**output);
        }

        // A change that cannot be applied is a problem of the project, shown in the console; the last frame
        // content keeps rendering. Only a failure to keep watching ends the editor.
        std::expected<void, Error> reload()
        {
            host::Diagnostics diagnostics;
            const auto        workspace = host::loadWorkspace(m_Location, m_Registry, diagnostics, m_Mode);
            if (workspace)
            {
                if (auto released = releaseDisplay(); !released)
                {
                    return released;
                }
                if (m_Runtime->load(*workspace, diagnostics))
                {
                    if (auto watched = m_Watcher.watch(workspace->watchDirectories); !watched)
                    {
                        return watched;
                    }
                    describe(*workspace);
                    log("reloaded " + workspace->project.name);
                }
            }
            if (!diagnostics.ok())
            {
                log("reload failed, keeping the previous frame\n" + diagnostics.text(m_Base));
            }
            return {};
        }

        std::expected<void, Error> onUpdate(float deltaSeconds) override
        {
            if (!m_Watcher.poll().empty())
            {
                if (auto reloaded = reload(); !reloaded)
                {
                    return reloaded;
                }
            }
            // The viewport window was laid out in the previous GUI frame; that frame has completed.
            const auto& session = m_Runtime->session();
            if (m_Viewport.width > 0 && m_Viewport.height > 0 &&
                (m_Viewport.width != session.size().width || m_Viewport.height != session.size().height))
            {
                if (auto released = releaseDisplay(); !released)
                {
                    return released;
                }
                host::Diagnostics diagnostics;
                if (!m_Runtime->resize(m_Viewport, diagnostics))
                {
                    log("resize failed\n" + diagnostics.text(m_Base));
                }
            }
            if (m_ViewportHovered)
            {
                // The mouse is over a GUI window, the viewport, so the GUI's own capture does not apply here.
                m_Camera.update(getWindow().input(), session.size());
            }
            // The project gets no input from the editor yet: the mouse of the window is not the viewport's.
            if (auto updated = m_Runtime->update(deltaSeconds, nullptr); !updated)
            {
                return updated;
            }
            if (m_Runtime->quitRequested())
            {
                getWindow().requestClose();
            }
            return {};
        }

        std::expected<void, Error> viewportWindow()
        {
            const auto* main = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos({main->Pos.x + 12, main->Pos.y + 12}, ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize({main->Size.x * 0.68f, main->Size.y * 0.66f}, ImGuiCond_FirstUseEver);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
            const bool open = ImGui::Begin("Viewport");
            ImGui::PopStyleVar();
            m_ViewportHovered = false;
            if (open)
            {
                const auto available = ImGui::GetContentRegionAvail();
                m_Viewport           = {uint32_t(std::clamp(available.x, 16.0f, 8192.0f)),
                                        uint32_t(std::clamp(available.y, 16.0f, 8192.0f))};
                const auto output    = m_Runtime->session().output("final");
                if (!output)
                {
                    ImGui::End();
                    return std::unexpected(output.error());
                }
                const auto image = getEditorGui().textureId(**output);
                if (!image)
                {
                    ImGui::End();
                    return std::unexpected(image.error());
                }
                ImGui::Image(*image, available);
                m_ViewportHovered = ImGui::IsItemHovered();
            }
            ImGui::End();
            return {};
        }

        std::expected<void, Error> parameterWindow()
        {
            const auto* main = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos({main->Pos.x + main->Size.x * 0.7f, main->Pos.y + 12}, ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize({main->Size.x * 0.29f, main->Size.y * 0.3f}, ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Pass parameters"))
            {
                const auto parameters = m_Runtime->session().parameters();
                if (parameters.empty())
                {
                    ImGui::TextDisabled("The project's graph has no parameters.");
                }
                for (const auto& parameter : parameters)
                {
                    const auto label = parameter.label + "##" + parameter.pass + "." + parameter.name;
                    auto       real  = float(parameter.value);
                    auto       whole = int(parameter.value);
                    const bool changed =
                        parameter.integer ?
                            ImGui::SliderInt(label.c_str(), &whole, int(parameter.minimum), int(parameter.maximum)) :
                            ImGui::SliderFloat(label.c_str(),
                                               &real,
                                               float(parameter.minimum),
                                               float(parameter.maximum));
                    if (changed)
                    {
                        // Applied before the next frame is recorded; the previous frame has completed.
                        if (auto set =
                                m_Runtime->session().setParameter(parameter.pass,
                                                                  parameter.name,
                                                                  parameter.integer ? double(whole) : double(real));
                            !set)
                        {
                            ImGui::End();
                            return set;
                        }
                    }
                }
            }
            ImGui::End();
            return {};
        }

        void resourceWindow()
        {
            const auto* main = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos({main->Pos.x + main->Size.x * 0.7f, main->Pos.y + main->Size.y * 0.33f},
                                    ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize({main->Size.x * 0.29f, main->Size.y * 0.34f}, ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Resources"))
            {
                ImGui::Text("Render path: %s", m_RenderPath.c_str());
                if (ImGui::BeginTable("resources", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
                {
                    ImGui::TableSetupColumn("Name");
                    ImGui::TableSetupColumn("Format");
                    ImGui::TableSetupColumn("Semantic");
                    ImGui::TableSetupColumn("Provided by");
                    ImGui::TableHeadersRow();
                    for (const auto& row : m_Resources)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(row.name.c_str());
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(row.format.c_str());
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(row.semantic.c_str());
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(row.provider.c_str());
                    }
                    ImGui::EndTable();
                }
                ImGui::SeparatorText("Passes of the frame");
                for (const auto& pass : m_Runtime->session().activePasses())
                {
                    ImGui::BulletText("%s", pass.c_str());
                }
            }
            ImGui::End();
        }

        void consoleWindow()
        {
            const auto* main = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos({main->Pos.x + 12, main->Pos.y + main->Size.y * 0.69f}, ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize({main->Size.x * 0.98f, main->Size.y * 0.29f}, ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Console"))
            {
                // A script that failed keeps its last state; its message stays until the script is fixed.
                if (const auto problems = m_Runtime->problems(); !problems.empty())
                {
                    ImGui::TextColored({1, 0.45f, 0.4f, 1}, "%s", problems.c_str());
                    ImGui::Separator();
                }
                for (const auto& line : m_Console)
                {
                    const bool failed = line.find("failed") != std::string::npos;
                    if (failed)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, {1, 0.45f, 0.4f, 1});
                    }
                    ImGui::TextWrapped("%s", line.c_str());
                    if (failed)
                    {
                        ImGui::PopStyleColor();
                    }
                }
                if (m_Console.size() != m_ShownLines)
                {
                    ImGui::SetScrollHereY(1);
                    m_ShownLines = m_Console.size();
                }
            }
            ImGui::End();
        }

        std::expected<void, Error> onImGui() override
        {
            ImGui::DockSpaceOverViewport();
            if (auto shown = viewportWindow(); !shown)
            {
                return shown;
            }
            if (auto shown = parameterWindow(); !shown)
            {
                return shown;
            }
            resourceWindow();
            consoleWindow();
            return m_Runtime->gui();
        }

        std::expected<void, Error> onRender(VriCommandBuffer* cmd, vultra::Texture& target) override
        {
            auto& session = m_Runtime->session();
            if (auto recorded = m_Runtime->record(cmd, m_Camera.camera(session.size())); !recorded)
            {
                return recorded;
            }
            const auto output = session.output("final");
            if (!output)
            {
                return std::unexpected(output.error());
            }
            // The GUI samples the project's image in the viewport window.
            (*output)->transition(
                cmd,
                {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_AllCommands});
            target.transition(
                cmd,
                {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
            const auto view = target.view();
            if (!view)
            {
                return std::unexpected(view.error());
            }
            const float clear[4] {0.05f, 0.05f, 0.06f, 1};
            vultra::beginColorPass(getDevice(), cmd, *view, getSwapchain().size(), clear);
            getDevice().core.CmdEndRendering(cmd);
            return drawGui(cmd, target);
        }

        // --capture saves the window of the last frame, or of the first one without --frames.
        std::expected<void, Error> onPostRender(vultra::Texture& target) override
        {
            if (m_Options.capture.empty() || frameCount() + 1 != std::max<uint64_t>(m_Options.frames, 1))
            {
                return {};
            }
            const auto image = vultra::readback(getDevice(), target);
            if (!image)
            {
                return std::unexpected(image.error());
            }
            return vultra::savePng(*image, m_Options.capture);
        }

        std::filesystem::path m_Location;
        std::filesystem::path m_Base;
        host::Mode            m_Mode;
        Options               m_Options;
        const host::Registry& m_Registry;
        // Destroyed before the window, device and GUI of the base class.
        std::unique_ptr<host::Runtime> m_Runtime;
        vultra::OrbitCamera            m_Camera;
        vultra::FileWatcher            m_Watcher;
        Extent                         m_Viewport {};
        bool                           m_ViewportHovered = false;
        std::string                    m_RenderPath;
        std::vector<ResourceRow>       m_Resources;
        std::vector<std::string>       m_Console;
        size_t                         m_ShownLines = 0;
    };

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
        // The window layout belongs to the project.
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
        const auto app = EditorApp::create(std::move(*services), registry, *workspace, options, **shaders);
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
                           parser.present<std::string>("--capture").value_or("")};
    if (auto result = run(options); !result)
    {
        std::cerr << "vultra-editor: " << result.error().message << '\n';
        return 1;
    }
    return 0;
}
