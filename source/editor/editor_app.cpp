#include "editor_app.hpp"

#include <vultra/core/base/logger.hpp>
#include <vultra/gpu/image_readback.hpp>
#include <vultra/host/shader_pass.hpp>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <iostream>

namespace vultra::editor
{
    namespace
    {
        constexpr const char* kParameterWindow = "Pass parameters";
        constexpr const char* kResourceWindow  = "Resources";
        constexpr const char* kConsoleWindow   = "Console";

        std::string viewportTitle(uint32_t number)
        {
            return "Viewport " + std::to_string(number);
        }

        bool sameSize(Extent a, Extent b)
        {
            return a.width == b.width && a.height == b.height;
        }
    } // namespace

    std::expected<std::unique_ptr<EditorApp>, Error> EditorApp::create(Services               services,
                                                                       const host::Registry&  registry,
                                                                       const host::Workspace& workspace,
                                                                       const Options&         options,
                                                                       const AssetSource&     shaders)
    {
        // The sizing viewport decides the size once the first GUI frame has laid it out.
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
            return makeError("{}", diagnostics.text(workspace.project.directory));
        }
        auto frame = Frame::create(services.device());
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        std::unique_ptr<EditorApp> app(
            new EditorApp(std::move(services), registry, workspace, options, std::move(*runtime), std::move(*frame)));
        if (auto watched = app->m_Watcher.watch(workspace.watchDirectories); !watched)
        {
            return std::unexpected(watched.error());
        }
        for (uint32_t index = 0; index < std::max(options.viewports, 1u); ++index)
        {
            app->addViewport();
        }
        app->describe(workspace);
        app->log("loaded " + workspace.project.name);
        return app;
    }

    EditorApp::EditorApp(Services                       services,
                         const host::Registry&          registry,
                         const host::Workspace&         workspace,
                         const Options&                 options,
                         std::unique_ptr<host::Runtime> runtime,
                         std::unique_ptr<Frame>         frame) :
        ImGuiApp(std::move(services), {.title = "Vultra Editor | " + workspace.project.name, .size = {1600, 900}}),
        m_Location(workspace.project.file),
        m_Base(workspace.project.directory),
        m_Mode(workspace.mode),
        m_Options(options),
        m_Registry(registry),
        m_Runtime(std::move(runtime)),
        m_Frame(std::move(frame))
    {
    }

    EditorApp::~EditorApp()
    {
        // The GUI may still hold the viewports' images of the last frame.
        getDevice().waitIdleForDestruction();
        for (auto& viewport : m_Viewports)
        {
            if (auto released = releaseImage(viewport); !released)
            {
                Logger::app().error("Release {}: {}", viewport.title, released.error().message);
            }
        }
    }

    void EditorApp::log(const std::string& text)
    {
        std::cout << "[vultra-editor] " << text << std::endl;
        m_Console.push_back(text);
    }

    // What the resource window lists, kept as text so it does not refer to a workspace that was replaced.
    void EditorApp::describe(const host::Workspace& workspace)
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

    // A change that cannot be applied is a problem of the project, shown in the console; the viewports keep
    // their last image. Only a failure to keep watching ends the editor.
    std::expected<void, Error> EditorApp::reload()
    {
        host::Diagnostics diagnostics;
        const auto        workspace = host::loadWorkspace(m_Location, m_Registry, diagnostics, m_Mode);
        if (workspace && m_Runtime->load(*workspace, diagnostics))
        {
            if (auto watched = m_Watcher.watch(workspace->watchDirectories); !watched)
            {
                return watched;
            }
            describe(*workspace);
            log("reloaded " + workspace->project.name);
        }
        if (!diagnostics.ok())
        {
            log("reload failed, keeping the previous frame\n" + diagnostics.text(m_Base));
        }
        return {};
    }

    void EditorApp::addViewport()
    {
        Viewport viewport;
        viewport.number             = m_NextViewport++;
        viewport.title              = viewportTitle(viewport.number);
        const auto& session         = m_Runtime->session();
        viewport.camera.center      = session.sceneCenter();
        viewport.camera.radius      = session.sceneRadius();
        viewport.camera.distance    = session.sceneRadius() * 2.4f;
        viewport.camera.verticalFov = glm::radians(45.0f);
        // Further viewports start from another side, so they do not repeat the first one.
        viewport.camera.yaw += 1.2f * float(viewport.number - 1);
        m_Viewports.push_back(std::move(viewport));
    }

    // The GUI lets go of an image before it is destroyed. The frames that showed it have completed.
    std::expected<void, Error> EditorApp::releaseImage(Viewport& viewport)
    {
        if (!viewport.image)
        {
            return {};
        }
        if (auto forgotten = getEditorGui().forgetTexture(*viewport.image); !forgotten)
        {
            return forgotten;
        }
        viewport.image.reset();
        return {};
    }

    // Between completed frames: drops closed viewports, follows the size of the sizing viewport's window and
    // gives every viewport an image of the session's size.
    std::expected<void, Error> EditorApp::syncViewports()
    {
        for (auto& viewport : m_Viewports)
        {
            if (!viewport.open)
            {
                if (auto released = releaseImage(viewport); !released)
                {
                    return released;
                }
            }
        }
        std::erase_if(m_Viewports,
                      [](const Viewport& viewport)
                      {
                          return !viewport.open;
                      });
        if (m_Viewports.empty())
        {
            return {};
        }
        auto sizing = std::ranges::find(m_Viewports, m_SizingViewport, &Viewport::number);
        if (sizing == m_Viewports.end())
        {
            sizing           = m_Viewports.begin();
            m_SizingViewport = sizing->number;
        }
        const Extent wanted = sizing->content;
        if (wanted.width > 0 && wanted.height > 0 && !sameSize(wanted, m_Runtime->session().size()))
        {
            host::Diagnostics diagnostics;
            if (!m_Runtime->resize(wanted, diagnostics))
            {
                log("resize failed\n" + diagnostics.text(m_Base));
            }
        }
        const Extent size = m_Runtime->session().size();
        for (auto& viewport : m_Viewports)
        {
            if (viewport.image && sameSize({viewport.image->desc.width, viewport.image->desc.height}, size))
            {
                continue;
            }
            if (auto released = releaseImage(viewport); !released)
            {
                return released;
            }
            auto image = Texture::create(getDevice(), colorTexture(size, VriFormat_RGBA8_UNORM));
            if (!image)
            {
                return std::unexpected(image.error());
            }
            viewport.image = std::move(*image);
        }
        return {};
    }

    // Records the project once per viewport with that viewport's camera and copies the display image into
    // the viewport's own. Each recording is a submission of its own: the frame graph keeps one set of
    // per-frame data.
    std::expected<void, Error> EditorApp::renderViewports()
    {
        auto&    session = m_Runtime->session();
        uint32_t view    = 0;
        for (auto& viewport : m_Viewports)
        {
            const auto cmd = m_Frame->begin();
            if (!cmd)
            {
                return std::unexpected(cmd.error());
            }
            if (auto recorded = m_Runtime->record(*cmd, viewport.camera.camera(session.size()), nullptr, view++);
                !recorded)
            {
                return recorded;
            }
            const auto output = session.output("final");
            if (!output)
            {
                return std::unexpected(output.error());
            }
            (*output)->transition(*cmd, {VriAccess_CopySourceRead, VriLayout_CopySource, VriPipelineStage_Transfer});
            viewport.image->transition(
                *cmd,
                {VriAccess_CopyDestinationWrite, VriLayout_CopyDestination, VriPipelineStage_Transfer});
            VriTextureCopyDesc copy {};
            copy.src.aspect   = VriImageAspect_Color;
            copy.dst.aspect   = VriImageAspect_Color;
            copy.src.layerNum = 1;
            copy.dst.layerNum = 1;
            getDevice().core.CmdCopyTexture(*cmd, viewport.image->handle, (*output)->handle, &copy);
            // The GUI samples it in the viewport's window.
            viewport.image->transition(
                *cmd,
                {VriAccess_ShaderResourceRead, VriLayout_ShaderResource, VriPipelineStage_AllCommands});
            if (auto completed = m_Frame->submitAndWait(); !completed)
            {
                return completed;
            }
        }
        return {};
    }

    std::expected<void, Error> EditorApp::onUpdate(float deltaSeconds)
    {
        if (!m_Watcher.poll().empty())
        {
            if (auto reloaded = reload(); !reloaded)
            {
                return reloaded;
            }
        }
        // The windows were laid out in the previous GUI frame; that frame has completed.
        if (auto synced = syncViewports(); !synced)
        {
            return synced;
        }
        for (auto& viewport : m_Viewports)
        {
            if (viewport.hovered)
            {
                // The mouse is over a GUI window, the viewport, so the GUI's own capture does not apply here.
                viewport.camera.update(getWindow().input(), viewport.content);
                m_SizingViewport = viewport.number;
            }
        }
        // The project gets no input from the editor yet: the mouse of the window is not a viewport's.
        if (auto updated = m_Runtime->update(deltaSeconds, nullptr); !updated)
        {
            return updated;
        }
        if (m_Runtime->quitRequested())
        {
            getWindow().requestClose();
        }
        return renderViewports();
    }

    void EditorApp::menuBar()
    {
        if (!ImGui::BeginMainMenuBar())
        {
            return;
        }
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Quit"))
            {
                getWindow().requestClose();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View"))
        {
            if (ImGui::MenuItem("New viewport"))
            {
                addViewport();
            }
            ImGui::Separator();
            for (auto& viewport : m_Viewports)
            {
                ImGui::MenuItem(viewport.title.c_str(), nullptr, &viewport.open);
            }
            ImGui::Separator();
            ImGui::MenuItem(kParameterWindow, nullptr, &m_ShowParameters);
            ImGui::MenuItem(kResourceWindow, nullptr, &m_ShowResources);
            ImGui::MenuItem(kConsoleWindow, nullptr, &m_ShowConsole);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset layout"))
            {
                m_ResetLayout = true;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // Viewports in the centre, parameters above resources on the right, the console along the bottom. Applied
    // when the project has no saved layout and on "Reset layout"; afterwards the user's docking is kept.
    void EditorApp::defaultLayout()
    {
        const ImGuiID dockspace = getEditorGui().dockspaceId();
        const auto*   node      = ImGui::DockBuilderGetNode(dockspace);
        const bool    untouched = node && !node->IsSplitNode() && node->Windows.empty();
        if (!m_ResetLayout && !untouched)
        {
            return;
        }
        m_ResetLayout = false;
        ImGui::DockBuilderRemoveNodeChildNodes(dockspace);
        ImGuiID       centre = dockspace;
        const ImGuiID right  = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.26f, nullptr, &centre);
        const ImGuiID bottom = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.26f, nullptr, &centre);
        ImGuiID       upper  = right;
        const ImGuiID lower  = ImGui::DockBuilderSplitNode(upper, ImGuiDir_Down, 0.55f, nullptr, &upper);
        // Two viewports stand side by side; further ones are tabs of the first.
        ImGuiID second = 0;
        if (m_Viewports.size() > 1)
        {
            second = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.5f, nullptr, &centre);
        }
        for (size_t index = 0; index < m_Viewports.size(); ++index)
        {
            ImGui::DockBuilderDockWindow(m_Viewports[index].title.c_str(), index == 1 ? second : centre);
        }
        ImGui::DockBuilderDockWindow(kParameterWindow, upper);
        ImGui::DockBuilderDockWindow(kResourceWindow, lower);
        ImGui::DockBuilderDockWindow(kConsoleWindow, bottom);
        ImGui::DockBuilderFinish(dockspace);
        m_ViewportDock = centre;
    }

    std::expected<void, Error> EditorApp::viewportWindow(Viewport& viewport)
    {
        // A viewport opened later joins the others as a tab; the user docks it where it is wanted.
        if (m_ViewportDock != 0)
        {
            ImGui::SetNextWindowDockID(m_ViewportDock, ImGuiCond_FirstUseEver);
        }
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
        const bool shown = ImGui::Begin(viewport.title.c_str(), &viewport.open);
        ImGui::PopStyleVar();
        viewport.hovered = false;
        if (shown)
        {
            if (viewport.number == m_Viewports.front().number && ImGui::IsWindowDocked())
            {
                m_ViewportDock = ImGui::GetWindowDockID();
            }
            const auto available = ImGui::GetContentRegionAvail();
            viewport.content     = {uint32_t(std::clamp(available.x, 16.0f, 8192.0f)),
                                    uint32_t(std::clamp(available.y, 16.0f, 8192.0f))};
            if (viewport.image)
            {
                const auto image = getEditorGui().textureId(*viewport.image);
                if (!image)
                {
                    ImGui::End();
                    return std::unexpected(image.error());
                }
                // Every viewport shows an image of the session's size; one that is not the sizing viewport
                // fits it into its window without distortion.
                const float  width  = float(viewport.image->desc.width);
                const float  height = float(viewport.image->desc.height);
                const float  scale  = std::min(available.x / width, available.y / height);
                const ImVec2 size {width * scale, height * scale};
                const auto   origin = ImGui::GetCursorPos();
                ImGui::SetCursorPos({origin.x + (available.x - size.x) / 2, origin.y + (available.y - size.y) / 2});
                ImGui::Image(*image, size);
                viewport.hovered = ImGui::IsItemHovered();
            }
        }
        ImGui::End();
        return {};
    }

    std::expected<void, Error> EditorApp::parameterWindow()
    {
        if (!m_ShowParameters)
        {
            return {};
        }
        if (ImGui::Begin(kParameterWindow, &m_ShowParameters))
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
                        ImGui::SliderFloat(label.c_str(), &real, float(parameter.minimum), float(parameter.maximum));
                if (changed)
                {
                    // Applied before the next frame is recorded; the previous frame has completed.
                    if (auto set = m_Runtime->session().setParameter(parameter.pass,
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

    void EditorApp::resourceWindow()
    {
        if (!m_ShowResources)
        {
            return;
        }
        if (ImGui::Begin(kResourceWindow, &m_ShowResources))
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

    void EditorApp::consoleWindow()
    {
        if (!m_ShowConsole)
        {
            return;
        }
        if (ImGui::Begin(kConsoleWindow, &m_ShowConsole))
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

    std::expected<void, Error> EditorApp::onImGui()
    {
        menuBar();
        defaultLayout();
        // Not a range-for: a window may not add to the list, but the menu did before this point.
        for (size_t index = 0; index < m_Viewports.size(); ++index)
        {
            if (!m_Viewports[index].open)
            {
                continue;
            }
            if (auto shown = viewportWindow(m_Viewports[index]); !shown)
            {
                return shown;
            }
        }
        if (auto shown = parameterWindow(); !shown)
        {
            return shown;
        }
        resourceWindow();
        consoleWindow();
        return m_Runtime->gui();
    }

    // The project was rendered into the viewports' images in onUpdate(); the window shows only the GUI.
    std::expected<void, Error> EditorApp::onRender(VriCommandBuffer* cmd, Texture& target)
    {
        target.transition(
            cmd,
            {VriAccess_ColorAttachmentWrite, VriLayout_ColorAttachment, VriPipelineStage_ColorAttachmentOutput});
        const auto view = target.view();
        if (!view)
        {
            return std::unexpected(view.error());
        }
        const float clear[4] {0.05f, 0.05f, 0.06f, 1};
        beginColorPass(getDevice(), cmd, *view, getSwapchain().size(), clear);
        getDevice().core.CmdEndRendering(cmd);
        return drawGui(cmd, target);
    }

    // --capture saves the window of the last frame, or of the first one without --frames.
    std::expected<void, Error> EditorApp::onPostRender(Texture& target)
    {
        if (m_Options.capture.empty() || frameCount() + 1 != std::max<uint64_t>(m_Options.frames, 1))
        {
            return {};
        }
        const auto image = readback(getDevice(), target);
        if (!image)
        {
            return std::unexpected(image.error());
        }
        return savePng(*image, m_Options.capture);
    }
} // namespace vultra::editor
