#include "vultra_editor/ui/windows/console_window.hpp"

#include <imgui.h>

namespace vultra
{
    namespace editor
    {
        ConsoleWindow::ConsoleWindow() : UIWindow("Console") {}

        ConsoleWindow::~ConsoleWindow() = default;

        void ConsoleWindow::onImGui()
        {
            ImGui::Begin(m_Name.c_str());
            ImGui::Text("This is the console window.");
            ImGui::End();
        }
    } // namespace editor
} // namespace vultra