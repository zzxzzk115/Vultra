#include "vultra_editor/ui/windows/scene_view_window.hpp"

#include <imgui.h>

namespace vultra
{
    namespace editor
    {
        SceneViewWindow::SceneViewWindow() : UIWindow("Scene View") {}

        SceneViewWindow::~SceneViewWindow() = default;

        void SceneViewWindow::onInit(rhi::RenderDevice& renderDevice)
        {
            UIWindow::onInit(renderDevice);

            // Default size
            m_SceneRenderTexture = rhi::Texture::Builder {}
                                       .setExtent({.width = 800, .height = 600})
                                       .setPixelFormat(rhi::PixelFormat::eRGBA8_UNorm)
                                       .setNumMipLevels(1)
                                       .setUsageFlags(rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled)
                                       .setupOptimalSampler(true)
                                       .build(renderDevice);
        }

        void SceneViewWindow::onImGui()
        {
            ImGui::Begin(m_Name.c_str());

            ImVec2 availSize = ImGui::GetContentRegionAvail();

            if (m_SceneTexture)
            {
                // Resize render texture if needed
                if (static_cast<uint32_t>(availSize.x) != m_SceneRenderTexture.getExtent().width ||
                    static_cast<uint32_t>(availSize.y) != m_SceneRenderTexture.getExtent().height)
                {
                    m_SceneRenderTexture =
                        rhi::Texture::Builder {}
                            .setExtent({.width  = static_cast<uint32_t>(availSize.x),
                                        .height = static_cast<uint32_t>(availSize.y)})
                            .setPixelFormat(rhi::PixelFormat::eRGBA8_UNorm)
                            .setNumMipLevels(1)
                            .setUsageFlags(rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled)
                            .setupOptimalSampler(true)
                            .build(*m_RenderDevice);

                    imgui::removeTexture(*m_RenderDevice, m_SceneTexture);
                    m_SceneTexture = imgui::addTexture(m_SceneRenderTexture);
                }
                ImGui::Image(m_SceneTexture, availSize);
            }
            else
            {
                m_SceneRenderTexture = rhi::Texture::Builder {}
                                           .setExtent({.width  = static_cast<uint32_t>(availSize.x),
                                                       .height = static_cast<uint32_t>(availSize.y)})
                                           .setPixelFormat(rhi::PixelFormat::eRGBA8_UNorm)
                                           .setNumMipLevels(1)
                                           .setUsageFlags(rhi::ImageUsage::eRenderTarget | rhi::ImageUsage::eSampled)
                                           .setupOptimalSampler(true)
                                           .build(*m_RenderDevice);
                m_SceneTexture = imgui::addTexture(m_SceneRenderTexture);
            }

            ImGui::End();
        }

        void SceneViewWindow::onRender(UIWindowRenderContext& ctx)
        {
            ctx.renderer->render(ctx.cb, &m_SceneRenderTexture, ctx.dt);
        }

        uint32_t SceneViewWindow::getViewportWidth() const { return m_SceneRenderTexture.getExtent().width; }

        uint32_t SceneViewWindow::getViewportHeight() const { return m_SceneRenderTexture.getExtent().height; }
    } // namespace editor
} // namespace vultra