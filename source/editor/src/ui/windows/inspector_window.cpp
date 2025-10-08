#include "vultra_editor/ui/windows/inspector_window.hpp"

#include <imgui.h>

namespace vultra
{
    namespace editor
    {
        InspectorWindow::InspectorWindow() : UIWindow("Inspector") {}

        InspectorWindow::~InspectorWindow() = default;

        void InspectorWindow::onImGui()
        {
            ImGui::Begin(m_Name.c_str());
            ImGui::Text("This is the inspector window.");
            ImGui::End();
        }
    } // namespace editor
} // namespace vultra