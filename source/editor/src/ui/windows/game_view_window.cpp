#include "vultra_editor/ui/windows/game_view_window.hpp"

#include <vultra/function/scenegraph/entity.hpp>
#include <vultra/function/scenegraph/logic_scene.hpp>

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>

namespace vultra
{
    using namespace imgui_literals;

    namespace editor
    {
        GameViewWindow::GameViewWindow() : UIWindow("Game View") {}

        GameViewWindow::~GameViewWindow() = default;

        void GameViewWindow::onInit(rhi::RenderDevice& renderDevice)
        {
            UIWindow::onInit(renderDevice);
            recreateRenderTexture(800, 600);
        }

        void GameViewWindow::onImGui()
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            m_IsWindowOpen = ImGui::Begin(m_Name.c_str());
            ImGui::PopStyleVar();

            if (!m_IsWindowOpen && !m_FirstFrame)
            {
                ImGui::End();
                m_FirstFrame = false;
                return;
            }

            float wheel = ImGui::GetIO().MouseWheel;

            // Ctrl + Mouse Wheel to zoom
            if (m_SelectedResolution != 0 && wheel != 0.0f && ImGui::GetIO().KeyCtrl)
            {
                float zoomStep = 0.1f;
                m_UserZoom += wheel * zoomStep;
                m_UserZoom = std::clamp(m_UserZoom, 0.25f, 4.0f);
            }

            // --- Toolbar ---
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.10f, 0.10f, 1.0f));
            ImGui::BeginChild("GameToolbar", ImVec2(0, 30_dpx), false, ImGuiWindowFlags_NoScrollbar);
            ImGui::PopStyleColor();
            drawToolbar();
            ImGui::EndChild();

            // --- Viewport ---
            ImGui::BeginChild("GameViewport", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

            ImVec2 avail = ImGui::GetContentRegionAvail();
            m_AvailSize  = glm::vec2(avail.x, avail.y);

            // Determine target aspect ratio
            auto  newTarget = computeTargetResolution(avail);
            float scale     = newTarget.z;
            m_TargetSize    = glm::vec2(newTarget.x, newTarget.y);

            ImVec2 renderSize(m_TargetSize.x * m_UserZoom, m_TargetSize.y * m_UserZoom);

            // Center the render area
            ImVec2 cursor = ImGui::GetCursorPos();
            if (renderSize.x < avail.x)
                cursor.x += (avail.x - renderSize.x) * 0.5f;
            if (renderSize.y < avail.y)
                cursor.y += (avail.y - renderSize.y) * 0.5f;
            ImGui::SetCursorPos(cursor);

            m_TargetSize *= scale;

            // Resize render texture & update main camera viewport size if needed
            if (static_cast<uint32_t>(m_TargetSize.x) != m_GameRenderTexture.getExtent().width ||
                static_cast<uint32_t>(m_TargetSize.y) != m_GameRenderTexture.getExtent().height)
            {
                auto  mainCamera               = m_LogicScene->getMainCamera();
                auto& cameraComponent          = mainCamera.getComponent<CameraComponent>();
                cameraComponent.viewPortWidth  = static_cast<uint32_t>(m_TargetSize.x);
                cameraComponent.viewPortHeight = static_cast<uint32_t>(m_TargetSize.y);
                recreateRenderTexture(static_cast<uint32_t>(m_TargetSize.x), static_cast<uint32_t>(m_TargetSize.y));
            }

            ImGui::Image(m_GameTexture, renderSize);

            ImGui::EndChild();
            ImGui::End();
        }

        void GameViewWindow::drawToolbar()
        {
            float windowHeight = ImGui::GetWindowHeight();

            float centerHeight = windowHeight * 0.5f;
            float offsetY      = (windowHeight - centerHeight) * 0.5f;
            ImGui::SetCursorPos(ImVec2(0, offsetY));

            ImGui::Indent();

            // --- Resolution Combo ---
            const char* resolutionLabels[] = {"Free Aspect", "16:9", "4:3", "21:9", "1920x1080", "1280x720", "800x600"};

            ImGui::SetNextItemWidth(120_dpx);
            if (ImGui::BeginCombo("##ResolutionCombo", resolutionLabels[m_SelectedResolution]))
            {
                for (int i = 0; i < IM_ARRAYSIZE(resolutionLabels); ++i)
                {
                    bool isSelected = (m_SelectedResolution == i);
                    if (ImGui::Selectable(resolutionLabels[i], isSelected))
                    {
                        m_SelectedResolution = i;

                        glm::vec3 newTarget = computeTargetResolution(ImVec2(m_AvailSize.x, m_AvailSize.y));
                        float     scale     = newTarget.z;

                        // Calculate auto zoom
                        float zoomX    = m_AvailSize.x / newTarget.x;
                        float zoomY    = m_AvailSize.y / newTarget.y;
                        float autoZoom = std::min(zoomX, zoomY);

                        // Skip zoom for Free Aspect
                        if (m_SelectedResolution == 0)
                            m_UserZoom = 1.0f;
                        else
                            m_UserZoom = autoZoom;

                        m_TargetSize = newTarget * scale;
                    }

                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine();

            // --- Zoom Slider ---
            // Hide zoom for free aspect
            if (m_SelectedResolution != 0)
            {
                ImGui::Text("Zoom:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(80_dpx);
                ImGui::SliderFloat("##ZoomSlider", &m_UserZoom, 0.25f, 4.0f, "%.2fx");
            }

            ImGui::SameLine(0, 16_dpx);
            ImGui::Text("Res: %dx%d",
                        static_cast<int>(m_GameRenderTexture.getExtent().width),
                        static_cast<int>(m_GameRenderTexture.getExtent().height));
        }

        glm::vec3 GameViewWindow::computeTargetResolution(const ImVec2& avail) const
        {
            float scale = 1.0f;

#if __APPLE__
            scale = os::Window::getActiveWindow().getDisplayScale();
#endif

            switch (m_SelectedResolution)
            {
                case 1: // 16:9
                {
                    float targetHeight = avail.x / (16.0f / 9.0f);
                    if (targetHeight > avail.y)
                        return {avail.y * (16.0f / 9.0f), avail.y, scale};
                    else
                        return {avail.x, targetHeight, scale};
                }
                case 2: // 4:3
                {
                    float targetHeight = avail.x / (4.0f / 3.0f);
                    if (targetHeight > avail.y)
                        return {avail.y * (4.0f / 3.0f), avail.y, scale};
                    else
                        return {avail.x, targetHeight, scale};
                }
                case 3: // 21:9
                {
                    float targetHeight = avail.x / (21.0f / 9.0f);
                    if (targetHeight > avail.y)
                        return {avail.y * (21.0f / 9.0f), avail.y, scale};
                    else
                        return {avail.x, targetHeight, scale};
                }
                case 4:
                    return {1920, 1080, 1.0f};
                case 5:
                    return {1280, 720, 1.0f};
                case 6:
                    return {800, 600, 1.0f};
                default:
                    return {avail.x, avail.y, scale};
            }
        }

        void GameViewWindow::onRender(UIWindowRenderContext& ctx)
        {
            if (m_IsWindowOpen)
            {
                m_LogicScene->setSimulationMode(LogicSceneSimulationMode::eGame);
            }
            ctx.renderer->setScene(m_LogicScene);
            ctx.renderer->render(ctx.cb, &m_GameRenderTexture, ctx.dt);
        }

        void GameViewWindow::recreateRenderTexture(uint32_t width, uint32_t height)
        {
            m_GameRenderTexture = rhi::Texture::Builder {}
                                      .setExtent({.width = width, .height = height})
                                      .setPixelFormat(rhi::PixelFormat::eRGBA8_UNorm)
                                      .setNumMipLevels(1)
                                      .setUsageFlags(rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled |
                                                     rhi::ImageUsage::eTransferDst)
                                      .setupOptimalSampler(true)
                                      .build(*m_RenderDevice);

            if (m_GameTexture)
                imgui::removeTexture(*m_RenderDevice, m_GameTexture);

            m_GameTexture = imgui::addTexture(m_GameRenderTexture);
        }
    } // namespace editor
} // namespace vultra