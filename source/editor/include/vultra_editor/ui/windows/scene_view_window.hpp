#pragma once

#include "vultra_editor/scripts/editor_camera_script.hpp"
#include "vultra_editor/ui/ui_window.hpp"

#include <vultra/function/renderer/imgui_renderer.hpp>
#include <vultra/function/scenegraph/entity.hpp>

#include <ImGuizmo/ImGuizmo.h>

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
            void recreateRenderTexture(uint32_t width, uint32_t height);

            void handleInput();
            void drawToolbar();

        private:
            imgui::ImGuiTextureID m_SceneTexture {nullptr};
            rhi::Texture          m_SceneRenderTexture;

            int            m_GuizmoOperation = ImGuizmo::OPERATION::TRANSLATE;
            ImGuizmo::MODE m_GuizmoMode      = ImGuizmo::MODE::LOCAL;

            Entity m_SelectedEntity;

            bool m_IsWindowHovered {false};
            bool m_IsWindowOpen    {false};

            EditorCameraScriptInstance m_EditorCameraScript;
        };
    } // namespace editor
} // namespace vultra