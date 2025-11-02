#pragma once

#include "vultra_editor/ui/ui_window.hpp"

#include <vultra/function/renderer/imgui_renderer.hpp>

namespace vultra
{
    namespace editor
    {
        class GameViewWindow final : public UIWindow
        {
        public:
            GameViewWindow();
            ~GameViewWindow() override;

            void onInit(rhi::RenderDevice& renderDevice) override;
            void onImGui() override;
            void onRender(UIWindowRenderContext& ctx) override;

        private:
            void recreateRenderTexture(uint32_t width, uint32_t height);

            void      drawToolbar();
            glm::vec3 computeTargetResolution(const ImVec2& avail) const;

        private:
            imgui::ImGuiTextureID m_GameTexture {nullptr};
            rhi::Texture          m_GameRenderTexture;

            bool m_IsWindowOpen {false};
            bool m_FirstFrame {true};

            glm::vec2 m_AvailSize {0, 0};
            glm::vec2 m_TargetSize {0, 0};

            float m_UserZoom {1.0f};

            uint32_t m_SelectedResolution {0}; // 0 = Free Aspect
        };
    } // namespace editor
} // namespace vultra