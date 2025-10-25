#pragma once

#include "vultra_editor/ui/ui_window.hpp"

#include <vultra/function/renderer/imgui_renderer.hpp>

namespace vultra
{
    namespace editor
    {
        class SceneViewWindow final : public UIWindow
        {
        public:
            SceneViewWindow();
            ~SceneViewWindow() override;

            void onInit(rhi::RenderDevice& renderDevice) override;
            void onImGui() override;
            void onRender(UIWindowRenderContext& ctx) override;

            uint32_t getViewportWidth() const;
            uint32_t getViewportHeight() const;

        private:
            imgui::ImGuiTextureID m_SceneTexture {nullptr};
            rhi::Texture          m_SceneRenderTexture;
        };
    } // namespace editor
} // namespace vultra