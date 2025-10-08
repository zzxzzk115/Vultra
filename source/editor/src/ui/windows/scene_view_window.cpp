#include "vultra_editor/ui/windows/scene_view_window.hpp"

#include <imgui.h>

namespace vultra
{
    namespace editor
    {
        SceneViewWindow::SceneViewWindow() : UIWindow("Scene View") {}

        SceneViewWindow::~SceneViewWindow() = default;

        void SceneViewWindow::onImGui()
        {
            ImGui::Begin(m_Name.c_str());
            ImGui::Text("This is the scene view window.");
            ImGui::End();
        }
    } // namespace editor
} // namespace vultra