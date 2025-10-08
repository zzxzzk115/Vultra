#include "vultra_editor/ui/windows/scene_graph_window.hpp"

#include <imgui.h>

namespace vultra
{
    namespace editor
    {
        SceneGraphWindow::SceneGraphWindow() : UIWindow("Scene Graph") {}

        SceneGraphWindow::~SceneGraphWindow() = default;

        void SceneGraphWindow::onImGui()
        {
            ImGui::Begin(m_Name.c_str());
            ImGui::Text("This is the scene graph window.");
            ImGui::End();
        }
    } // namespace editor
} // namespace vultra