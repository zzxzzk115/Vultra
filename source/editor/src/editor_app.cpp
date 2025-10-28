#include "vultra_editor/editor_app.hpp"
#include "vultra_editor/asset/asset_database.hpp"
#include "vultra_editor/ui/windows/asset_browser_window.hpp"
#include "vultra_editor/ui/windows/console_window.hpp"
#include "vultra_editor/ui/windows/game_view_window.hpp"
#include "vultra_editor/ui/windows/inspector_window.hpp"
#include "vultra_editor/ui/windows/scene_graph_window.hpp"
#include "vultra_editor/ui/windows/scene_view_window.hpp"
#include "vultra_editor/version.hpp"

#include <vultra/core/base/common_context.hpp>
#include <vultra/function/scenegraph/entity.hpp>

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace vultra
{
    namespace editor
    {
        EditorApp::EditorApp(const std::span<char*>& args) :
            ImGuiApp(
                args,
                {.title = "Vultra Editor", .width = 1280, .height = 720},
                {.imguiIniFile = nullptr, .setDockSpace = [this](ImGuiDockNodeFlags flags) { buildDockSpace(flags); }}),
            m_Renderer(*m_RenderDevice, m_Swapchain.getFormat())
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

            // TODO: Remove, test code
            auto  camera                    = m_EditingScene.createMainCamera();
            auto& camTransform              = camera.getComponent<TransformComponent>();
            auto& camComponent              = camera.getComponent<CameraComponent>();
            camTransform.position           = glm::vec3(0.0f, 0.0f, 5.0f);
            camComponent.clearFlags         = CameraClearFlags::eSkybox;
            camComponent.environmentMapPath = (std::filesystem::path(projectPath).parent_path() /
                                               "Assets/Textures/EnvMaps/citrus_orchard_puresky_1k.hdr")
                                                  .generic_string();
            auto rawMesh = m_EditingScene.createRawMeshEntity(
                "DamagedHelmet",
                (std::filesystem::path(projectPath).parent_path() / "Assets/Models/DamagedHelmet/DamagedHelmet.gltf")
                    .generic_string());
            auto& rawMeshTransform = rawMesh.getComponent<TransformComponent>();
            rawMeshTransform.setRotationEuler({0.0f, 45.0f, 0.0f});

            // Initialize Asset Database
            AssetDatabase::get()->initialize(m_CurrentProject, *m_RenderDevice);

            // Register UI Windows
            m_UIWindowManager.registerWindow<SceneGraphWindow>();
            m_UIWindowManager.registerWindow<SceneViewWindow>();
            m_UIWindowManager.registerWindow<GameViewWindow>();
            m_UIWindowManager.registerWindow<AssetBrowserWindow>();
            m_UIWindowManager.registerWindow<ConsoleWindow>();
            m_UIWindowManager.registerWindow<InspectorWindow>();

            // Initialize UIWindowManager
            m_UIWindowManager.onInit(*m_RenderDevice);

            // Log initial message
            VULTRA_CLIENT_INFO("Vultra Editor: version {} initialized.", EDITOR_VERSION_STRING);
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
            m_Renderer.setScene(&m_EditingScene);
            m_UIWindowManager.onUpdate(dt, &m_EditingScene);

            // TODO: Remove, test code
            auto mainCamera = m_EditingScene.getMainCamera();
            if (mainCamera)
            {
                auto& cameraComponent = m_EditingScene.getMainCamera().getComponent<CameraComponent>();
                auto* sceneViewWindow = m_UIWindowManager.getWindowOfType<SceneViewWindow>();
                assert(sceneViewWindow);
                cameraComponent.viewPortWidth  = sceneViewWindow->getViewportWidth();
                cameraComponent.viewPortHeight = sceneViewWindow->getViewportHeight();
            }

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
            m_UIWindowManager.onPreRender();
            ImGuiApp::onPreRender();
        }

        void EditorApp::onRender(rhi::CommandBuffer& cb, const rhi::RenderTargetView rtv, const fsec dt)
        {
            // const auto& [frameIndex, target] = rtv;
            UIWindowRenderContext ctx {.cb = cb, .renderer = &m_Renderer, .rtv = rtv, .dt = dt};
            m_UIWindowManager.onRender(ctx);
            ImGuiApp::onRender(cb, rtv, dt);
        }

        void EditorApp::onPostRender()
        {
            m_UIWindowManager.onPostRender();
            ImGuiApp::onPostRender();
        }

        void EditorApp::onImGui()
        {
            drawMainMenuBar();
            m_UIWindowManager.onImGui();

            if (m_ShowAboutPopup)
            {
                ImGui::OpenPopup("About Vultra Editor");
                m_ShowAboutPopup = false;
            }

            if (ImGui::BeginPopupModal("About Vultra Editor", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Vultra Editor");
                ImGui::Separator();
                ImGui::Text("Version: %s", EDITOR_VERSION_STRING);
                ImGui::Text("Contributors: Lazy_V (Kexuan Zhang)");
                ImGui::Text("License: MIT");
                ImGui::TextLinkOpenURL(ICON_MDI_GITHUB "GitHub", "https://github.com/zzxzzk115/Vultra");
                ImGui::Spacing();
                if (ImGui::Button("Close"))
                {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

#if _DEBUG
            if (m_ShowDemoWindow)
            {
                ImGui::ShowDemoWindow();
            }
#endif
            ImGuiApp::onImGui();
        }

        void EditorApp::buildDockSpace(ImGuiDockNodeFlags dockSpaceFlags)
        {
            float   displayScale = os::Window::getPrimaryDisplayScale();
            ImGuiID dockSpaceId  = ImGui::GetID("DockSpace");

            ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(displayScale * 320.0f, displayScale * 240.0f));
            ImGui::DockSpace(dockSpaceId, ImVec2(0.0f, 0.0f), dockSpaceFlags);
            ImGui::PopStyleVar();

            // Only build default layout the first time
            static bool first_time = true;
            if (!first_time)
            {
                return;
            }
            first_time = false;

            ImGui::DockBuilderRemoveNode(dockSpaceId);
            ImGui::DockBuilderAddNode(dockSpaceId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockSpaceId, ImGui::GetMainViewport()->Size);

            // Docking Layout:
            ImGuiID dock_main_id = dockSpaceId;
            ImGuiID dock_left_id =
                ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.75f, nullptr, &dock_main_id);
            ImGuiID dock_right_id = dock_main_id; // Inspector
            ImGuiID dock_left_top_id =
                ImGui::DockBuilderSplitNode(dock_left_id, ImGuiDir_Up, 0.70f, nullptr, &dock_left_id);
            ImGuiID dock_left_bottom_id = dock_left_id;
            ImGuiID dock_left_top_left_id =
                ImGui::DockBuilderSplitNode(dock_left_top_id, ImGuiDir_Left, 0.25f, nullptr, &dock_left_top_id);
            ImGuiID dock_left_top_right_id = dock_left_top_id;

            auto* sceneGraphWindow   = m_UIWindowManager.getWindowOfType<SceneGraphWindow>();
            auto* sceneViewWindow    = m_UIWindowManager.getWindowOfType<SceneViewWindow>();
            auto* gameViewWindow     = m_UIWindowManager.getWindowOfType<GameViewWindow>();
            auto* assetBrowserWindow = m_UIWindowManager.getWindowOfType<AssetBrowserWindow>();
            auto* consoleWindow      = m_UIWindowManager.getWindowOfType<ConsoleWindow>();
            auto* inspectorWindow    = m_UIWindowManager.getWindowOfType<InspectorWindow>();

            if (sceneGraphWindow)
                ImGui::DockBuilderDockWindow(sceneGraphWindow->getName().c_str(), dock_left_top_left_id);

            if (sceneViewWindow)
                ImGui::DockBuilderDockWindow(sceneViewWindow->getName().c_str(), dock_left_top_right_id);
            if (gameViewWindow)
                ImGui::DockBuilderDockWindow(gameViewWindow->getName().c_str(), dock_left_top_right_id);

            if (assetBrowserWindow)
                ImGui::DockBuilderDockWindow(assetBrowserWindow->getName().c_str(), dock_left_bottom_id);
            if (consoleWindow)
                ImGui::DockBuilderDockWindow(consoleWindow->getName().c_str(), dock_left_bottom_id);

            if (inspectorWindow)
                ImGui::DockBuilderDockWindow(inspectorWindow->getName().c_str(), dock_right_id);

            // Finalize layout
            ImGui::DockBuilderFinish(dockSpaceId);
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

#if _DEBUG
                    ImGui::Separator();
                    ImGui::MenuItem("ImGui Demo Window", nullptr, &m_ShowDemoWindow);
#endif

                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Help"))
                {
                    if (ImGui::MenuItem("About Vultra Editor"))
                    {
                        m_ShowAboutPopup = true;
                    }

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