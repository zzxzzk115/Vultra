#include "vultra_editor/editor_app.hpp"
#include "vultra_editor/ui/windows/asset_browser_window.hpp"
#include "vultra_editor/ui/windows/console_window.hpp"
#include "vultra_editor/ui/windows/game_view_window.hpp"
#include "vultra_editor/ui/windows/inspector_window.hpp"
#include "vultra_editor/ui/windows/scene_graph_window.hpp"
#include "vultra_editor/ui/windows/scene_view_window.hpp"

#include <vultra/core/base/common_context.hpp>

#include <imgui.h>

namespace vultra
{
    namespace editor
    {
        EditorApp::EditorApp(const std::span<char*>& args) :
            ImGuiApp(args, {.title = "Vultra Editor", .width = 1280, .height = 720})
        {
            m_CurrentProject.name = "NewVultraProject";

            VULTRA_CLIENT_TRACE("ArgCount: {}", args.size());
            for (uint32_t i = 0; i < args.size(); ++i)
            {
                VULTRA_CLIENT_TRACE("Arg[{}]: {}", i, args[i]);
            }

            m_ArgParser.add_description("Vultra Engine Editor Application");
            m_ArgParser.add_argument("--project", "Path to the project file").default_value(std::string(""));

            try
            {
                m_ArgParser.parse_args(args.size(), args.data());
            }
            catch (const std::exception& e)
            {
                VULTRA_CLIENT_ERROR("Failed to parse arguments: {}", e.what());
                throw;
            }

            auto projectPath = m_ArgParser.get<std::string>("--project");

            if (projectPath.empty())
            {
                VULTRA_CLIENT_WARN("No project file specified.");
            }
            else
            {
                auto projectDir = std::filesystem::path(projectPath).parent_path().generic_string();
                if (!engine::loadProject(projectPath, m_CurrentProject))
                {
                    VULTRA_CLIENT_ERROR("Failed to load project file: {}", projectPath);
                    // Create a default project
                    if (!std::filesystem::exists(projectDir))
                    {
                        std::filesystem::create_directories(projectDir);
                    }
                    engine::createDefaultProject(projectDir, m_CurrentProject.name);
                }
                else
                {
                    VULTRA_CLIENT_INFO("Loaded project: {} at {}", m_CurrentProject.name, m_CurrentProject.directory);
                    m_CurrentProject.directory = projectDir;
                }
            }

            // Initialize Asset Database
            AssetDatabase::get()->initialize(m_CurrentProject, *m_RenderDevice);

            // Register UI Windows
            m_UIWindowManager.registerWindow<ConsoleWindow>();
            m_UIWindowManager.registerWindow<AssetBrowserWindow>();
            m_UIWindowManager.registerWindow<GameViewWindow>();
            m_UIWindowManager.registerWindow<InspectorWindow>();
            m_UIWindowManager.registerWindow<SceneGraphWindow>();
            m_UIWindowManager.registerWindow<SceneViewWindow>();

            // Initialize UIWindowManager
            m_UIWindowManager.onInit();
        }

        EditorApp::~EditorApp()
        {
            m_UIWindowManager.onDestroy();
            AssetDatabase::destroy();
        }

        void EditorApp::onPreUpdate(const fsec dt)
        {
            m_UIWindowManager.onPreUpdate();
            ImGuiApp::onPreUpdate(dt);
        }

        void EditorApp::onUpdate(const fsec dt)
        {
            m_UIWindowManager.onUpdate(dt);
            ImGuiApp::onUpdate(dt);
        }

        void EditorApp::onPhysicsUpdate(const fsec dt)
        {
            m_UIWindowManager.onPhysicsUpdate(dt);
            ImGuiApp::onPhysicsUpdate(dt);
        }

        void EditorApp::onPostUpdate(const fsec dt)
        {
            m_UIWindowManager.onPostUpdate(dt);
            ImGuiApp::onPostUpdate(dt);
        }

        void EditorApp::onPreRender()
        {
            UIWindowRenderContext ctx {};
            m_UIWindowManager.onPreRender(ctx);
            ImGuiApp::onPreRender();
        }

        void EditorApp::onRender(rhi::CommandBuffer& cb, const rhi::RenderTargetView rtv, const fsec dt)
        {
            // const auto& [frameIndex, target] = rtv;
            UIWindowRenderContext ctx {};
            m_UIWindowManager.onRender(ctx);
            ImGuiApp::onRender(cb, rtv, dt);
        }

        void EditorApp::onPostRender()
        {
            UIWindowRenderContext ctx {};
            m_UIWindowManager.onPostRender(ctx);
            ImGuiApp::onPostRender();
        }

        void EditorApp::onImGui()
        {
            drawMainMenuBar();
            m_UIWindowManager.onImGui();
            ImGuiApp::onImGui();
        }

        void EditorApp::drawMainMenuBar()
        {
            if (ImGui::BeginMainMenuBar())
            {
                if (ImGui::BeginMenu("File"))
                {
                    if (ImGui::MenuItem("Exit", "Alt+F4"))
                    {
                        close();
                    }
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Window"))
                {
                    for (const auto& w : m_UIWindowManager.getWindows())
                        ImGui::MenuItem(w->getName().c_str(), nullptr, &w->isOpen());
                    ImGui::EndMenu();
                }

#ifdef VULTRA_ENABLE_RENDERDOC
                if (m_RenderDocAPI->isAvailable())
                {
                    if (ImGui::BeginMenu("RenderDoc"))
                    {
                        if (ImGui::MenuItem("Capture Frame"))
                        {
                            m_WantCaptureFrame = true;
                        }
                        ImGui::EndMenu();
                    }
                }
#endif

                ImGui::EndMainMenuBar();
            }
        }
    } // namespace editor
} // namespace vultra