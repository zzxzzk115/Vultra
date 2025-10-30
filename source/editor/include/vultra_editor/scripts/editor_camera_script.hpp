#pragma once

#include "vultra/function/scripting/internal/internal_script.hpp"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace vultra
{
    namespace editor
    {
        class EditorCameraScriptInstance : public InternalScriptInstance
        {
        public:
            EditorCameraScriptInstance() : InternalScriptInstance(true) {}
            ~EditorCameraScriptInstance() override = default;

            void onImGui();

            void setWindowHovered(bool windowHovered) { m_IsWindowHovered = windowHovered; }
            void setGrabMoveEnabled(bool grabMoveEnabled) { m_IsGrabMoveEnabled = grabMoveEnabled; }

        private:
            float m_Sensitivity = 0.05f;
            float m_BaseSpeed   = 0.1f;

            glm::vec2 m_LastFrameMousePosition;

            bool m_IsWindowHovered   = false;
            bool m_IsFreeMoveValid   = false;
            bool m_IsGrabMoveValid   = false;
            bool m_IsGrabMoveEnabled = false;

            bool m_FirstMouse = true;
            bool m_FirstGrab  = true;

            friend class SceneViewWindow;
        };
    } // namespace editor
} // namespace vultra